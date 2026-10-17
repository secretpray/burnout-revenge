// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Private renderer diagnostic. Included after the renderer's state/helpers.
// BR_FG_CAPTURE_AT=<seconds>: two adjacent GPU frames, never enabled by UI.
// Readbacks and disk I/O deliberately perturb timing: don't benchmark with it.
        struct TemporalReadback
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceSize size = 0;
            bool coherent = false;
            std::string name;
        };
        struct TemporalProbe
        {
            std::filesystem::path path;
            std::ofstream events, constants, pixelConstants, fetches;
            std::vector<TemporalReadback> images;
            std::unordered_set<uint64_t> shaders, pixelShaders, uiTargets;
            uint32_t draws = 0, omitted = 0;
            uint64_t bytes = 0;
        };
        std::unique_ptr<TemporalProbe> s_temporal;

        // Candidates only. A screen-space shader can also draw menus, fades
        // or world labels. Neither this predicate nor a draw ordinal is a
        // validated HUD boundary or stable object identifier.
        bool TemporalScreenPixels()
        {
            const auto& vi = s_vs->info;
            if (vi.positionFetch == ~0u || vi.positionStride < 2 ||
                (!vi.constRelative && (vi.constUsed[0] & 3) != 3)) return false;
            float c[8];
            memcpy(c, &s_regs[XE_GPU_REG_SHADER_CONSTANT_000_X], sizeof(c));
            return c[3] == 1 && c[6] == 0 && c[7] == 0 && c[4] != 0 && c[5] != 0 &&
                std::fabs(-2 * c[0] / c[4] - 1280) < 1 && std::fabs(-2 * c[1] / c[5] - 720) < 1;
        }

        void TemporalImage(VkImage image, VkFormat format, VkRect2D rect, const char* kind, uint32_t base, uint32_t pitch)
        {
            if (!s_temporal || !image || !rect.extent.width || !rect.extent.height) return;
            auto& p = *s_temporal;
            const bool depth = format == VK_FORMAT_D32_SFLOAT_S8_UINT;
            uint32_t bpp = (format == VK_FORMAT_R16G16B16A16_SFLOAT || format == VK_FORMAT_R16G16B16A16_UNORM) ? 8 : 4;
            if (!depth && format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R16G16B16A16_SFLOAT &&
                format != VK_FORMAT_R16G16B16A16_UNORM && format != VK_FORMAT_R32_SFLOAT &&
                format != VK_FORMAT_R16G16_SFLOAT && format != VK_FORMAT_A2B10G10R10_UNORM_PACK32) { p.omitted++; return; }
            const VkDeviceSize size = uint64_t(rect.extent.width) * rect.extent.height * bpp;
            if (p.images.size() >= 48 || size > (96ull << 20) - p.bytes) { p.omitted++; return; }
            TemporalReadback read;
            read.size = size;
            read.name = std::format("image_{}.bin", p.images.size());
            VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bi.size = size; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if (vkCreateBuffer(s_dev, &bi, nullptr, &read.buffer) != VK_SUCCESS) { p.omitted++; return; }
            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(s_dev, read.buffer, &req);
            uint32_t type = UINT32_MAX; int best = -1;
            for (uint32_t i = 0; i < s_memProps.memoryTypeCount; ++i)
            {
                const auto flags = s_memProps.memoryTypes[i].propertyFlags;
                if (!(req.memoryTypeBits & (1u << i)) || !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
                int score = ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? 2 : 0) +
                    ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 1 : 0);
                if (score > best) { best = score; type = i; }
            }
            VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            ai.allocationSize = req.size; ai.memoryTypeIndex = type;
            if (type == UINT32_MAX || vkAllocateMemory(s_dev, &ai, nullptr, &read.memory) != VK_SUCCESS ||
                vkBindBufferMemory(s_dev, read.buffer, read.memory, 0) != VK_SUCCESS)
            {
                vkDestroyBuffer(s_dev, read.buffer, nullptr);
                if (read.memory) vkFreeMemory(s_dev, read.memory, nullptr);
                p.omitted++; return;
            }
            read.coherent = s_memProps.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            EndPass(); Begin(); FullBarrier();
            VkBufferImageCopy copy{};
            copy.imageSubresource = { depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            copy.imageOffset = { rect.offset.x, rect.offset.y, 0 };
            copy.imageExtent = { rect.extent.width, rect.extent.height, 1 };
            vkCmdCopyImageToBuffer(s_cmd, image, VK_IMAGE_LAYOUT_GENERAL, read.buffer, 1, &copy);
            VkBufferMemoryBarrier host{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = read.buffer; host.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(s_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 0, nullptr, 1, &host, 0, nullptr);
            FullBarrier();
            p.events << std::format("{{\"type\":\"image\",\"file\":\"{}\",\"kind\":\"{}\",\"format\":{},\"width\":{},\"height\":{},\"x\":{},\"y\":{},\"base\":{},\"pitch\":{},\"after_draw\":{},\"bytes\":{}}}\n",
                read.name, kind, uint32_t(format), rect.extent.width, rect.extent.height,
                rect.offset.x, rect.offset.y, base, pitch, p.draws, size);
            p.bytes += size;
            p.images.push_back(std::move(read));
        }

        void TemporalTarget(RenderTarget* rt, VkRect2D rect, const char* kind)
        {
            if (!rt) return;
            const int32_t x0 = std::clamp(rect.offset.x, 0, int32_t(rt->width));
            const int32_t y0 = std::clamp(rect.offset.y, 0, int32_t(rt->height));
            const int32_t x1 = std::clamp(rect.offset.x + int32_t(rect.extent.width), x0, int32_t(rt->width));
            const int32_t y1 = std::clamp(rect.offset.y + int32_t(rect.extent.height), y0, int32_t(rt->height));
            TemporalImage(rt->image.image, rt->vkFormat, {{x0,y0},{uint32_t(x1-x0),uint32_t(y1-y0)}}, kind, rt->base, rt->pitch);
        }

        void TemporalDraw(uint32_t count, uint32_t primitive, VkRect2D scissor)
        {
            if (!s_temporal) return;
            auto& p = *s_temporal;
            if (p.draws >= 8192) { p.omitted++; return; }
            const auto& vi = s_vs->info;
            const uint32_t fetch = vi.positionFetch < 96 ? vi.positionFetch : 0;
            const uint32_t* vf = &s_regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + fetch * 2];
            p.events << std::format("{{\"type\":\"draw\",\"draw\":{},\"vs\":\"{:016x}\",\"ps\":\"{:016x}\",\"count\":{},\"primitive\":{},\"depth_control\":{},\"color_info\":{},\"depth_info\":{},\"surface\":{},\"screen_pixels\":{},\"position_fetch\":{},\"stride_words\":{},\"vertex_base\":{},\"constants_offset\":{},\"scissor\":[{},{},{},{}]}}\n",
                p.draws, s_vs->hash, s_ps->hash, count, primitive, s_regs[XE_GPU_REG_RB_DEPTHCONTROL],
                s_regs[XE_GPU_REG_RB_COLOR_INFO], s_regs[XE_GPU_REG_RB_DEPTH_INFO], s_regs[XE_GPU_REG_RB_SURFACE_INFO],
                TemporalScreenPixels() ? "true" : "false", vi.positionFetch, vi.positionStride,
                vi.positionFetch < 96 ? vf[0] & 0x1FFFFFFCu : 0, uint64_t(p.draws) * 4096,
                scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height);
            p.constants.write(reinterpret_cast<const char*>(&s_regs[XE_GPU_REG_SHADER_CONSTANT_000_X]), 4096);
            p.pixelConstants.write(reinterpret_cast<const char*>(&s_regs[XE_GPU_REG_SHADER_CONSTANT_000_X + 1024]), 4096);
            p.fetches.write(reinterpret_cast<const char*>(&s_regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0]), 768);
            ++p.draws;
            if (p.shaders.insert(s_vs->hash).second)
            {
                p.events << std::format("{{\"type\":\"shader\",\"vs\":\"{:016x}\",\"relative_constants\":{},\"const_used\":[\"{:016x}\",\"{:016x}\",\"{:016x}\",\"{:016x}\"]}}\n",
                    s_vs->hash, vi.constRelative ? "true" : "false", vi.constUsed[0], vi.constUsed[1], vi.constUsed[2], vi.constUsed[3]);
                std::ofstream shader(p.path / std::format("vs_{:016x}.glsl", s_vs->hash));
                shader << vi.glsl;
            }
            if (p.pixelShaders.insert(s_ps->hash).second)
            {
                const auto& pi = s_ps->info;
                p.events << std::format("{{\"type\":\"pixel_shader\",\"ps\":\"{:016x}\",\"targets\":{},\"textures_2d\":{},\"relative_constants\":{},\"const_used\":[\"{:016x}\",\"{:016x}\",\"{:016x}\",\"{:016x}\"]}}\n",
                    s_ps->hash, pi.colorTargetMask, pi.texture2DMask, pi.constRelative ? "true" : "false",
                    pi.constUsed[0], pi.constUsed[1], pi.constUsed[2], pi.constUsed[3]);
                std::ofstream shader(p.path / std::format("ps_{:016x}.glsl", s_ps->hash));
                shader << pi.glsl;
            }
            // Raw register bits avoid turning NaNs into invalid JSON and
            // preserve the guest viewport/depth convention for inspection.
            p.events << std::format("{{\"type\":\"viewport\",\"draw\":{},\"vte\":{},\"window_offset\":{},\"mode\":{},\"vertex_control\":{},\"bits\":[{},{},{},{},{},{}]}}\n", p.draws - 1,
                s_regs[XE_GPU_REG_PA_CL_VTE_CNTL], s_regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET], s_regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL], s_regs[XE_GPU_REG_PA_SU_VTX_CNTL],
                s_regs[XE_GPU_REG_PA_CL_VPORT_XSCALE], s_regs[XE_GPU_REG_PA_CL_VPORT_XOFFSET],
                s_regs[XE_GPU_REG_PA_CL_VPORT_YSCALE], s_regs[XE_GPU_REG_PA_CL_VPORT_YOFFSET],
                s_regs[XE_GPU_REG_PA_CL_VPORT_ZSCALE], s_regs[XE_GPU_REG_PA_CL_VPORT_ZOFFSET]);
        }

        void TemporalFinish()
        {
            if (!s_temporal) return;
            Flush();  // every readback fence completes before mapping/freeing
            auto& p = *s_temporal;
            for (auto& read : p.images)
            {
                void* data = nullptr;
                bool ok = vkMapMemory(s_dev, read.memory, 0, VK_WHOLE_SIZE, 0, &data) == VK_SUCCESS;
                if (ok && !read.coherent)
                {
                    VkMappedMemoryRange range{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
                    range.memory = read.memory; range.size = VK_WHOLE_SIZE;
                    ok = vkInvalidateMappedMemoryRanges(s_dev, 1, &range) == VK_SUCCESS;
                }
                if (ok)
                {
                    std::ofstream file(p.path / read.name, std::ios::binary);
                    file.write(static_cast<const char*>(data), read.size);
                    ok = bool(file);
                }
                if (!ok) p.omitted++;
                if (data) vkUnmapMemory(s_dev, read.memory);
                vkDestroyBuffer(s_dev, read.buffer, nullptr);
                vkFreeMemory(s_dev, read.memory, nullptr);
            }
            const bool streamError = !p.events || !p.constants || !p.pixelConstants || !p.fetches;
            p.events << std::format("{{\"type\":\"end\",\"draws\":{},\"images\":{},\"omitted\":{},\"stream_error\":{}}}\n",
                p.draws, p.images.size(), p.omitted, streamError ? "true" : "false");
            fprintf(stderr, "[temporal] %s: %u draws, %zu images, %u omitted%s; diagnostic only\n",
                p.path.string().c_str(), p.draws, p.images.size(), p.omitted, streamError ? ", I/O error" : "");
            s_temporal.reset();
        }

        void TemporalOnSwap(uint64_t frame)
        {
            static const double at = [] {
                const char* v = std::getenv("BR_FG_CAPTURE_AT");
                char* end = nullptr;
                const double n = v ? std::strtod(v, &end) : -1;
                return v && end != v && *end == 0 && std::isfinite(n) && n >= 0 ? n : -1;
            }();
            if (at < 0) return;
            TemporalFinish();
            static const auto start = std::chrono::steady_clock::now();
            static uint32_t taken = 0;
            if (taken >= 2 || std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < at) return;
            ++taken;
            auto p = std::make_unique<TemporalProbe>();
            p->path = std::format("build/temporal_f{}", frame);
            std::error_code ec;
            std::filesystem::create_directories(p->path, ec);
            p->events.open(p->path / "events.jsonl");
            p->constants.open(p->path / "vs_constants.bin", std::ios::binary);
            p->pixelConstants.open(p->path / "ps_constants.bin", std::ios::binary);
            p->fetches.open(p->path / "texture_fetches.bin", std::ios::binary);
            if (ec || !p->events || !p->constants || !p->pixelConstants || !p->fetches)
            {
                fprintf(stderr, "[temporal] cannot create capture folder: %s\n", p->path.string().c_str());
                return;
            }
            const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            p->events << std::format("{{\"type\":\"frame\",\"schema\":1,\"frame\":{},\"steady_ns\":{},\"guest_vblank\":{},\"constants_encoding\":\"uint32-host-le-float-bits\",\"ps_bytes_per_draw\":4096,\"fetch_bytes_per_draw\":768,\"motion_validated\":false,\"hud_separated\":false}}\n",
                frame, ns, video::GuestVblanks());
            s_temporal = std::move(p);
        }
