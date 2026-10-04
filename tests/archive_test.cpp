#include "lwg_decoder.h"
#include "xflarchive.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using Bytes = std::vector<uint8_t>;
void put32(Bytes& data, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) data[offset+i] = static_cast<uint8_t>(value>>(i*8));
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function, const char* message) {
    try { function(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        using namespace liarsoft;
        Bytes shortLwg(16, 0); shortLwg[0]='L'; shortLwg[1]='G'; shortLwg[2]=1;
        rejects([&]{LwgDecoder::decode(shortLwg,"CP932");}, "Truncated LWG header accepted");
        Bytes lwg(49,0); lwg[0]='L'; lwg[1]='G'; lwg[2]=1;
        put32(lwg,4,1); put32(lwg,8,1); put32(lwg,12,1); put32(lwg,20,19);
        put32(lwg,37,2); lwg[41]=1; lwg[42]='a'; lwg[47]='W'; lwg[48]='G';
        require(LwgDecoder::decode(lwg,"CP932").entries[0].data==Bytes({'W','G'}),"Valid LWG rejected");
        for(size_t length=0;length<lwg.size();++length) {
            auto bad=lwg; bad.resize(length);
            rejects([&]{LwgDecoder::decode(bad,"CP932");},"Truncated LWG accepted");
        }
        auto bad=lwg; put32(bad,20,0xffffffffu);
        rejects([&]{LwgDecoder::decode(bad,"CP932");},"Overflowing LWG table accepted");
        bad=lwg; bad[41]=255;
        rejects([&]{LwgDecoder::decode(bad,"CP932");},"LWG name outside table accepted");
        bad=lwg; put32(bad,33,0xffffffffu);
        rejects([&]{LwgDecoder::decode(bad,"CP932");},"LWG offset outside payload accepted");

        XflArchive archive; archive.encoding="CP932";
        for(const auto* name : {"2.gsc","10.gsc","1.gsc","B.wcg","a.wcg"})
            archive.entries.push_back({name,{1,2,3}});
        const auto packed=archive.toBytes();
        const auto decoded=XflArchive::fromBytes(packed,"CP932");
        const std::vector<std::string> expected{"1.gsc","10.gsc","2.gsc","a.wcg","B.wcg"};
        for(size_t i=0;i<expected.size();++i)
            require(decoded.entries[i].fileName==expected[i],"XFL table is not sorted for engine string lookup");
        for(size_t length=0;length<packed.size();++length) {
            auto broken=packed; broken.resize(length);
            rejects([&]{XflArchive::fromBytes(broken,"CP932");},"Truncated XFL accepted");
        }
        auto broken=packed; put32(broken,44,0xffffffffu);
        rejects([&]{XflArchive::fromBytes(broken,"CP932");},"Negative XFL offset accepted");
        broken=packed; put32(broken,4,0);
        rejects([&]{XflArchive::fromBytes(broken,"CP932");},"XFL entries outside declared table accepted");
        broken=packed; put32(broken,4,0x7fffffffu);
        rejects([&]{XflArchive::fromBytes(broken,"CP932");},"Oversized XFL table accepted");
        archive.entries={{std::string(32,'a'),{1}}};
        rejects([&]{archive.toBytes();},"Overlong XFL name silently truncated");
        archive.entries={{std::string(14*3,'\0'),{1}}};
        archive.entries[0].fileName="ああああああああああああああ.wcg"; // 32 CP932 bytes.
        rejects([&]{archive.toBytes();},"Overlong CP932 name accepted");
        archive.entries={{"A.wcg",{1}},{"a.wcg",{2}}};
        rejects([&]{archive.toBytes();},"Case-colliding XFL names accepted");
        for(const auto* name : {"../a.wcg","/tmp/a.wcg","..\\a.wcg","C:a.wcg"}) {
            archive.entries={{name,{1}}};
            rejects([&]{archive.toBytes();},"Unsafe XFL output name accepted");
            broken=packed;
            std::fill(broken.begin()+12,broken.begin()+44,0);
            std::copy(name,name+std::char_traits<char>::length(name),broken.begin()+12);
            rejects([&]{XflArchive::fromBytes(broken,"CP932");},"Unsafe XFL input name accepted");
        }
        // Packing must not wrap the one-byte LWG name length.
        if(argc==2) {
            const std::filesystem::path directory(argv[1]);
            std::filesystem::create_directories(directory);
            LwgDecoder::Archive controls;
            controls.width=850; controls.height=26;
            controls.entries={
                {"slide", {'W','G'}, 690, 4, 8, 2},
                {"slide", {}, 7, -1, 8, 0},
                {"slide_lev_f", {}, -10, 12, 8, 0}
            };
            const auto controlDirectory=directory/"controls";
            LwgDecoder::extractToDirectory(controls,controlDirectory.string(),"CP932");
            const auto restored=LwgDecoder::decode(
                LwgPacker::pack(controlDirectory.string(),"CP932"),"CP932");
            require(restored.width==controls.width && restored.height==controls.height &&
                    restored.entries.size()==controls.entries.size(),
                    "Metadata-only LWG entries lost during directory round trip");
            for(size_t i=0;i<controls.entries.size();++i) {
                const auto& original=controls.entries[i];
                const auto& actual=restored.entries[i];
                require(actual.name==original.name && actual.x==original.x &&
                        actual.y==original.y && actual.flag==original.flag &&
                        actual.data==original.data,
                        "Empty LWG entry changed or reused a same-named image");
            }
            controls.entries.erase(controls.entries.begin());
            const auto emptyDirectory=directory/"empty_controls";
            LwgDecoder::extractToDirectory(controls,emptyDirectory.string(),"CP932");
            rejects([&]{LwgPacker::pack(emptyDirectory.string(),"CP932");},
                    "Archive containing only empty entries must still be rejected");
            std::ofstream(directory/"a.wcg",std::ios::binary)<<"WG";
            std::ofstream(directory/".meta.xml")
                <<"<Canvas><Width>1</Width><Height>1</Height><Items>"
                <<"<Item x=\"0\" y=\"0\" flag=\"40\">a</Item>"
                <<"<Item x=\"0\" y=\"0\" flag=\"40\">"<<std::string(256,'x')
                <<"</Item></Items></Canvas>";
            rejects([&]{LwgPacker::pack(directory.string(),"CP932");},"LWG name length wrapped");
        }
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n'; return 1;
    }
    return 0;
}
