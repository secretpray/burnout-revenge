// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// GPU-only temporal-input producer, private to renderer.cpp. No Metal
// consumer/presentation is enabled here. All writes share the renderer's
// Vulkan queue; consumers on another queue will require explicit leases/events.
        struct HistorySlot
        {
            Image scene, ui, depth, motion;
            uint64_t frame = ~0ull, epoch = 0;
            std::array<float,16> camera{};
        };
        struct HistoryState
        {
            std::array<HistorySlot,3> slots;
            Image scene, depth, encoded;
            uint32_t width=0, height=0, latest=0;
            uint64_t frame=0, epoch=0;
            std::atomic<uint64_t> accepted{0}, incomplete{0}, resets{0}, resizeWaits{0};
            bool sceneCopied=false, pair=false, failed=false, exportable=false;
            gpu::temporal::Coverage depthTiles, velocityTiles;
            struct Candidate { std::array<uint32_t,16> bits{}; uint32_t count=0; };
            std::array<Candidate,8> cameras{};
            VkDescriptorSetLayout set=VK_NULL_HANDLE;
            VkPipelineLayout layout=VK_NULL_HANDLE;
            VkPipeline pipeline=VK_NULL_HANDLE;
        } s_history;
        std::atomic<bool> s_historyStopping{false};
        std::unique_ptr<RenderTarget> s_trueVelocity;
        uint64_t s_trueVelocityFrame=~0ull;

        RenderTarget* HistoryMotionTarget(RenderTarget* color)
        {
            if (s_historyStopping.load(std::memory_order_relaxed)) return nullptr;
            const uint32_t width=color->scale.x.Size(1280), height=color->scale.y.Size(720);
            if (width>1920 || height>1080) return nullptr;
            if (!s_trueVelocity || s_trueVelocity->width!=width || s_trueVelocity->height!=height)
            {
                if (s_trueVelocity) { Flush(); ++s_history.resizeWaits; DestroyTarget(*s_trueVelocity); }
                auto target=std::make_unique<RenderTarget>();
                target->base=4095; target->format=uint32_t(xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT);
                target->pitch=1280; target->depth=false; target->wide=false;
                target->width=width; target->height=height; target->guestWidth=1280; target->guestHeight=720;
                target->scale=color->scale; target->vkFormat=VK_FORMAT_R16G16B16A16_SFLOAT; target->blendable=false;
                target->image=CreateImage(VK_IMAGE_TYPE_2D,target->vkFormat,{width,height,1},1,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_VIEW_TYPE_2D,VK_IMAGE_ASPECT_COLOR_BIT);
                target->sampleView=target->image.view;
                EndPass(); Begin(); ToGeneral(target->image.image,VK_IMAGE_ASPECT_COLOR_BIT,1);
                s_trueVelocity=std::move(target); s_trueVelocityFrame=~0ull;
                fprintf(stderr,"[history] private true-motion MRT2 %ux%u, homogeneous previous/current UV; game MRT0/1 preserved\n",width,height);
            }
            if (s_trueVelocityFrame!=s_frame)
            {
                EndPass(); Begin(); FullBarrier();
                const VkClearColorValue zero{};
                const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
                vkCmdClearColorImage(s_cmd,s_trueVelocity->image.image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);
                FullBarrier(); s_trueVelocityFrame=s_frame;
            }
            return s_trueVelocity.get();
        }

        void HistoryDestroyPictures()
        {
            auto& h=s_history;
            // Caller has completed this queue. Nothing exposes these images
            // to another thread/queue in this preparatory producer.
            for (auto& slot:h.slots)
            {
                DestroyImage(slot.scene); DestroyImage(slot.ui);
                DestroyImage(slot.depth); DestroyImage(slot.motion);
                slot.frame=~0ull;
            }
            DestroyImage(h.scene); DestroyImage(h.depth); DestroyImage(h.encoded);
            h.width=h.height=0; h.pair=false;
        }

        Image HistoryImage(VkFormat format, uint32_t width, uint32_t height, bool depth=false)
        {
            const auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|
                VK_IMAGE_USAGE_TRANSFER_DST_BIT|(depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_STORAGE_BIT);
            const void* next=nullptr;
#ifdef __APPLE__
            VkExportMetalObjectCreateInfoEXT metal{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
            metal.exportObjectType=VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
            if (s_history.exportable) next=&metal;
#endif
            auto image=CreateImage(VK_IMAGE_TYPE_2D,format,{width,height,1},1,usage,VK_IMAGE_VIEW_TYPE_2D,
                depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,0,1,next);
            ToGeneral(image.image,depth ? VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT,1);
            return image;
        }

        bool HistoryEnsure(RenderTarget* source)
        {
            auto& h=s_history;
            if (h.failed) return false;
            const uint32_t width=source->scale.x.Size(1280), height=source->scale.y.Size(720);
            // Bound this prototype's memory; larger history is a later measured option.
            if (width>1920 || height>1080) return false;
            if (h.width==width && h.height==height) return true;
            if (h.width) { Flush(); ++h.resizeWaits; HistoryDestroyPictures(); }
            EndPass(); Begin();
#ifdef __APPLE__
            h.exportable=vkGetDeviceProcAddr(s_dev,"vkExportMetalObjectsEXT") != nullptr;
#endif
            if (!h.pipeline)
            {
                const char* glsl=R"(#version 460
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform sampler2D sceneIn;
layout(set=0,binding=1) uniform sampler2D uiIn;
layout(set=0,binding=2) uniform sampler2D depthIn;
layout(set=0,binding=3) uniform sampler2D encodedIn;
layout(set=0,binding=8) uniform sampler2D trueMotionIn;
layout(rgba16f,set=0,binding=4) writeonly uniform image2D sceneOut;
layout(rgba16f,set=0,binding=5) writeonly uniform image2D uiOut;
layout(r32f,set=0,binding=6) writeonly uniform image2D depthOut;
layout(rg16f,set=0,binding=7) writeonly uniform image2D motionOut;
layout(push_constant) uniform P { uvec2 size; } p;
void main() {
    ivec2 xy=ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(uvec2(xy),p.size))) return;
    vec4 v=texelFetch(encodedIn,xy,0);
    vec2 motion=(v.rb+v.ga-1.0)*vec2(p.size);
    vec4 actual=texelFetch(trueMotionIn,xy,0);
    if (actual.z>0.5) motion=actual.xy*vec2(p.size);
    float depth=texelFetch(depthIn,xy,0).r;
    // The background has no geometry/object velocity. Don't turn an
    // unwritten split-sign clear into a full-screen displacement.
    if (depth<=0.0 || any(isnan(motion)) || any(isinf(motion))) motion=vec2(0);
    imageStore(sceneOut,xy,vec4(texelFetch(sceneIn,xy,0).rgb,1));
    imageStore(uiOut,xy,vec4(texelFetch(uiIn,xy,0).rgb,1));
    imageStore(depthOut,xy,vec4(depth));
    imageStore(motionOut,xy,vec4(motion,0,0));
})";
                VkDescriptorSetLayoutBinding bindings[9]{};
                for (uint32_t i=0;i<9;++i)
                    bindings[i]={i,(i<4 || i==8) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                        1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
                CreateComputePipeline(glsl,bindings,9,8,h.set,h.layout,h.pipeline);
            }
            for (auto& slot:h.slots)
            {
                slot.scene=HistoryImage(VK_FORMAT_R16G16B16A16_SFLOAT,width,height);
                slot.ui=HistoryImage(VK_FORMAT_R16G16B16A16_SFLOAT,width,height);
                slot.depth=HistoryImage(VK_FORMAT_R32_SFLOAT,width,height);
                slot.motion=HistoryImage(VK_FORMAT_R16G16_SFLOAT,width,height);
            }
            h.scene=HistoryImage(VK_FORMAT_R8G8B8A8_UNORM,width,height);
            h.depth=HistoryImage(VK_FORMAT_D32_SFLOAT_S8_UINT,width,height,true);
            h.encoded=HistoryImage(VK_FORMAT_R16G16B16A16_UNORM,width,height);
            h.width=width; h.height=height; ++h.epoch; h.pair=false;
#if defined(__APPLE__) && !TARGET_OS_IOS
            const auto& first=h.slots[0];
            h.exportable=platform::metalfx::ValidateTemporalInputs({first.scene.image,first.ui.image,
                first.depth.image,first.motion.image,width,height});
#endif
            h.depthTiles.Reset(width,height); h.velocityTiles.Reset(width,height); h.sceneCopied=false;
            uint64_t bytes=h.scene.bytes+h.depth.bytes+h.encoded.bytes;
            if (s_trueVelocity) bytes+=s_trueVelocity->image.bytes;
            for (const auto& slot:h.slots) bytes+=slot.scene.bytes+slot.ui.bytes+slot.depth.bytes+slot.motion.bytes;
            fprintf(stderr,"[history] GPU inputs %ux%u, 3 slots, %.1f MiB, Metal-exportable %u; no live FG\n",
                width,height,double(bytes)/(1024*1024),unsigned(h.exportable));
            return true;
        }

        void HistoryCopy(VkImage source, VkImage dest, VkRect2D rect, bool depth=false)
        {
            EndPass(); Begin(); FullBarrier();
            VkImageCopy copy{};
            copy.srcSubresource=copy.dstSubresource={depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            copy.srcOffset=copy.dstOffset={rect.offset.x,rect.offset.y,0};
            copy.extent={rect.extent.width,rect.extent.height,1};
            vkCmdCopyImage(s_cmd,source,VK_IMAGE_LAYOUT_GENERAL,dest,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
            FullBarrier();
        }

        void HistoryResolve(RenderTarget* source, VkRect2D rect)
        {
            if (s_historyStopping.load(std::memory_order_relaxed)) return;
            const bool depth=source->depth && source->base==512 && source->vkFormat==VK_FORMAT_D32_SFLOAT_S8_UINT;
            const bool motion=!source->depth && source->base==1024 && source->vkFormat==VK_FORMAT_R16G16B16A16_UNORM;
            if ((!depth && !motion) || source->pitch!=1280 || rect.extent.width<640 || rect.extent.height<128) return;
            if (!HistoryEnsure(source)) return;
            auto& h=s_history;
            auto& coverage=depth ? h.depthTiles : h.velocityTiles;
            if (rect.offset.x<0 || rect.offset.y<0 ||
                !coverage.Add({uint32_t(rect.offset.x),uint32_t(rect.offset.y),rect.extent.width,rect.extent.height})) return;
            HistoryCopy(source->image.image,depth ? h.depth.image : h.encoded.image,rect,depth);
        }

        void HistoryBeforeUi(RenderTarget* target, VkRect2D rect)
        {
            if (s_historyStopping.load(std::memory_order_relaxed)) return;
            auto& h=s_history;
            if (!h.width || h.sceneCopied || target->base!=0 || target->pitch!=1280 ||
                target->vkFormat!=VK_FORMAT_R8G8B8A8_UNORM || !h.depthTiles.Complete() || !h.velocityTiles.Complete() ||
                !Contains(rect,{{0,0},{h.width,h.height}})) return;
            HistoryCopy(target->image.image,h.scene.image,{{0,0},{h.width,h.height}});
            h.sceneCopied=true;
        }

        void HistoryRecordDraw()
        {
            if (s_historyStopping.load(std::memory_order_relaxed)) return;
            const auto& vi=s_vs->info;
            if (vi.constRelative || (vi.constUsed[0]&15)!=15 || (s_ps->info.colorTargetMask&3)!=3 ||
                !(s_regs[XE_GPU_REG_RB_DEPTHCONTROL]&2) || (s_regs[XE_GPU_REG_RB_COLOR_INFO]&4095)!=0 ||
                (s_regs[XE_GPU_REG_RB_DEPTH_INFO]&4095)!=512 || (s_regs[XE_GPU_REG_RB_SURFACE_INFO]&0x3fff)!=1280) return;
            std::array<uint32_t,16> bits;
            memcpy(bits.data(),&s_regs[XE_GPU_REG_SHADER_CONSTANT_000_X],64);
            auto& candidates=s_history.cameras;
            for (auto& c:candidates) if (c.count && c.bits==bits) { ++c.count; return; }
            for (auto& c:candidates) if (!c.count) { c.bits=bits; c.count=1; return; }
            // Bounded heavy-hitter tracking; never allocates per draw/frame.
            for (auto& c:candidates) --c.count;
        }

        void HistoryCommit(const Image& front, uint32_t width, uint32_t height)
        {
            if (s_historyStopping.load(std::memory_order_relaxed)) return;
            auto& h=s_history;
            const auto camera=std::max_element(h.cameras.begin(),h.cameras.end(),[](const auto& a,const auto& b){return a.count<b.count;});
            if (!h.width || width!=h.width || height!=h.height || !h.sceneCopied ||
                !h.depthTiles.Complete() || !h.velocityTiles.Complete() || camera->count<8 ||
                !s_trueVelocity || s_trueVelocityFrame!=h.frame)
            { ++h.incomplete; h.pair=false; return; }
            const uint32_t index=uint32_t(h.accepted%h.slots.size());
            auto& slot=h.slots[index];
            std::array<float,16> matrix; memcpy(matrix.data(),camera->bits.data(),64);
            const auto& previous=h.slots[h.latest];
            h.pair=h.accepted && previous.frame+1==h.frame && previous.epoch==h.epoch &&
                !gpu::temporal::CameraCut(previous.camera,matrix);
            if (h.accepted && !h.pair) { ++h.epoch; ++h.resets; }
            EndPass(); Begin(); FullBarrier();
            VkDescriptorImageInfo images[9]{};
            const VkImageView views[]={h.scene.view,front.view,h.depth.view,h.encoded.view,
                slot.scene.view,slot.ui.view,slot.depth.view,slot.motion.view,s_trueVelocity->image.view};
            VkWriteDescriptorSet writes[9]{};
            for (uint32_t i=0;i<9;++i)
            {
                const bool sampled=i<4 || i==8;
                images[i]={sampled ? s_sampler : VK_NULL_HANDLE,views[i],VK_IMAGE_LAYOUT_GENERAL};
                writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,nullptr,VK_NULL_HANDLE,i,0,1,
                    sampled ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&images[i],nullptr,nullptr};
            }
            ++s_stateEpoch;
            vkCmdBindPipeline(s_cmd,VK_PIPELINE_BIND_POINT_COMPUTE,h.pipeline);
            s_pushDescriptorSet(s_cmd,VK_PIPELINE_BIND_POINT_COMPUTE,h.layout,0,9,writes);
            const uint32_t size[]={width,height};
            vkCmdPushConstants(s_cmd,h.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(size),size);
            vkCmdDispatch(s_cmd,(width+7)/8,(height+7)/8,1);
            FullBarrier();
            slot.frame=h.frame; slot.epoch=h.epoch; slot.camera=matrix;
            h.latest=index; ++h.accepted;
            if (h.accepted==1 || h.frame%120==0)
                fprintf(stderr,"[history] frame %llu slot %u epoch %llu pair %u, accepted %llu incomplete %llu resets %llu; GPU-only\n",
                    (unsigned long long)h.frame,index,(unsigned long long)h.epoch,unsigned(h.pair),
                    (unsigned long long)h.accepted,(unsigned long long)h.incomplete,(unsigned long long)h.resets);
            if (s_temporal)
            {
                s_temporal->events << std::format("{{\"type\":\"history_meta\",\"frame\":{},\"slot\":{},\"epoch\":{},\"pair\":{},\"metal_exportable\":{},\"live_fg\":false}}\n",
                    slot.frame,index,slot.epoch,h.pair ? "true" : "false",h.exportable ? "true" : "false");
                const VkRect2D rect{{0,0},{width,height}};
                TemporalImage(slot.scene.image,VK_FORMAT_R16G16B16A16_SFLOAT,rect,"history-scene",index,0);
                TemporalImage(slot.ui.image,VK_FORMAT_R16G16B16A16_SFLOAT,rect,"history-ui",index,0);
                TemporalImage(slot.depth.image,VK_FORMAT_R32_SFLOAT,rect,"history-depth",index,0);
                TemporalImage(slot.motion.image,VK_FORMAT_R16G16_SFLOAT,rect,"history-motion",index,0);
                TemporalImage(s_trueVelocity->image.image,VK_FORMAT_R16G16B16A16_SFLOAT,rect,"true-motion-source",0,0);
            }
        }

        void HistoryOnSwap(uint64_t frame)
        {
            if (s_historyStopping.load(std::memory_order_relaxed)) return;
            auto& h=s_history;
            h.frame=frame; h.sceneCopied=false; h.cameras={};
            h.depthTiles.Reset(h.width,h.height); h.velocityTiles.Reset(h.width,h.height);
        }

        void HistoryShutdown()
        {
            auto& h=s_history;
            s_historyStopping.store(true,std::memory_order_relaxed);
            // renderer::Shutdown runs on the main thread while the detached
            // command processor is still alive. Like the renderer's other
            // targets/pipelines, these resources live through process exit.
            // Never Flush/Destroy them here from the wrong thread. Resize
            // retirement remains explicit on the command processor above.
            fprintf(stderr,"[history] stopping: accepted %llu incomplete %llu resets %llu resize waits %llu; resources retained through process exit\n",
                (unsigned long long)h.accepted.load(),(unsigned long long)h.incomplete.load(),
                (unsigned long long)h.resets.load(),(unsigned long long)h.resizeWaits.load());
        }
