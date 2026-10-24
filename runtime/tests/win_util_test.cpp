#include "win_util.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <vector>

int main() {
    using rscript::quote_argument;
    assert(quote_argument(L"") == L"\"\"");
    assert(quote_argument(L"a b") == L"\"a b\"");
    assert(quote_argument(L"a\\") == L"\"a\\\\\"");
    assert(quote_argument(L"a\\\"b") == L"\"a\\\\\\\"b\"");
    assert(rscript::utf16(rscript::utf8(L"路径 Русский").c_str()) == L"路径 Русский");
    std::vector<unsigned char> data(sizeof(rscript::HostInitPacket) + 8);
    auto* packet = reinterpret_cast<rscript::HostInitPacket*>(data.data());
    packet->total_size = static_cast<uint32_t>(data.size());
    uint32_t offset = sizeof(*packet);
    const wchar_t value[] = L"abc";
    std::memcpy(data.data() + offset, value, sizeof(value));
    assert(std::wstring(rscript::packet_string(packet, offset)) == L"abc");
    assert(!rscript::packet_string(packet, offset - 1));
    assert(!rscript::packet_string(packet, UINT32_MAX));
    assert(!rscript::packet_string(packet, packet->total_size));
    std::memset(data.data() + offset, 0xff, sizeof(value));
    assert(!rscript::packet_string(packet, offset));
    packet->total_size = 0;
    assert(!rscript::packet_string(packet, offset));
}
