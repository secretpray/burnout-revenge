// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace guestformat
{
    // Xbox CRT integer/string subset. va_list entries occupy big-endian 64-bit
    // slots, including 32-bit arguments. No host va_list or wchar_t is reused.
    // Unsupported conversions return false, never consume the wrong argument.
    template<class Next, class ReadString>
    bool Wide(const std::u16string& format, Next next, ReadString readString, std::u16string& output)
    {
        output.clear();
        for (size_t i = 0; i < format.size(); ++i)
        {
            if (format[i] != u'%') { output += format[i]; continue; }
            if (++i == format.size()) return false;
            if (format[i] == u'%') { output += u'%'; continue; }
            std::string spec = "%";
            bool left = false;
            while (i < format.size() && std::u16string(u"-+ #0").find(format[i]) != std::u16string::npos)
            {
                left |= format[i] == u'-';
                spec += char(format[i++]);
            }
            int width = 0, precision = -1;
            if (i < format.size() && format[i] == u'*')
            {
                width = int32_t(next()); ++i;
                if (width < -4096 || width > 4096) return false;
                if (width < 0) { left = true; spec += '-'; width = -width; }
                if (width > 4096) return false;
                spec += std::to_string(width);
            }
            else while (i < format.size() && format[i] >= u'0' && format[i] <= u'9')
            {
                width = width * 10 + format[i] - u'0';
                if (width > 4096) return false;
                spec += char(format[i++]);
            }
            if (i < format.size() && format[i] == u'.')
            {
                ++i; precision = 0;
                if (i < format.size() && format[i] == u'*') { precision = int32_t(next()); ++i; }
                else while (i < format.size() && format[i] >= u'0' && format[i] <= u'9')
                {
                    precision = precision * 10 + format[i++] - u'0';
                    if (precision > 4096) return false;
                }
                if (precision > 4096) return false;
                if (precision >= 0) spec += "." + std::to_string(precision);
            }
            bool wideInteger = false, narrowString = false;
            if (format.compare(i, 3, u"I64") == 0) { wideInteger = true; i += 3; }
            else if (format.compare(i, 2, u"ll") == 0) { wideInteger = true; i += 2; }
            else if (i < format.size() && (format[i] == u'h' || format[i] == u'l'))
            { narrowString = format[i] == u'h'; ++i; }
            if (i == format.size()) return false;
            char conversion = char(format[i]);
            std::u16string piece;
            if (conversion == 's' || conversion == 'S')
            {
                piece = readString(uint32_t(next()), narrowString || conversion == 'S');
                if (precision >= 0 && piece.size() > size_t(precision)) piece.resize(precision);
                if (piece.size() < size_t(width))
                    piece.insert(left ? piece.size() : 0, width - piece.size(), u' ');
            }
            else if (conversion == 'c') piece += char16_t(next());
            else if (std::string("diuoxX").find(conversion) != std::string::npos)
            {
                if (narrowString) return false; // short integers not in this subset
                uint64_t value = next();
                spec += "ll"; spec += conversion;
                char buffer[8192];
                int count;
                if (conversion == 'd' || conversion == 'i')
                    count = snprintf(buffer, sizeof(buffer), spec.c_str(), (long long)(wideInteger ? int64_t(value) : int64_t(int32_t(value))));
                else
                    count = snprintf(buffer, sizeof(buffer), spec.c_str(), (unsigned long long)(wideInteger ? value : uint32_t(value)));
                if (count < 0 || count >= int(sizeof(buffer))) return false;
                for (int j = 0; j < count; ++j) piece += char16_t(buffer[j]);
            }
            else return false;
            output += piece;
            if (output.size() > 65536) return false;
        }
        return true;
    }
}
