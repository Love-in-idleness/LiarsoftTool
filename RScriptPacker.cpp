#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <cctype>

namespace fs = std::filesystem;

// 兼容 Windows/Linux 的类型定义
typedef uint32_t DWORD;
typedef unsigned char BYTE;
typedef uint32_t ULONG;

// 文件头结构体（紧凑对齐）
#pragma pack(push, 1)
typedef struct Header {
    DWORD Magic;          // 固定为 0x0001424c
    DWORD ChunkSize;      // ChunkItem 数组总大小
    DWORD ChunkCount;     // 文件数量
} Header;
#pragma pack(pop)

// 文件项结构体
#pragma pack(push, 1)
typedef struct ChunkItem {
    char FileName[0x20];  // 文件名（31字符+1终止符）
    DWORD Offset;         // 相对于数据块起始的偏移
    DWORD Size;           // 文件大小
} ChunkItem;
#pragma pack(pop)

// 自然排序比较函数（Human sorting）
bool natural_sort_compare(const std::string& a, const std::string& b) {
    static const std::regex re("(\\d+)");
    
    auto it_a = std::sregex_token_iterator(a.begin(), a.end(), re, 0);
    auto it_b = std::sregex_token_iterator(b.begin(), b.end(), re, 0);
    auto end = std::sregex_token_iterator();

    while (it_a != end && it_b != end) {
        std::string part_a = *it_a++;
        std::string part_b = *it_b++;
        
        // 如果两部分都是数字，按数值比较
        if (std::all_of(part_a.begin(), part_a.end(), ::isdigit) &&
            std::all_of(part_b.begin(), part_b.end(), ::isdigit)) {
            int num_a = std::stoi(part_a);
            int num_b = std::stoi(part_b);
            if (num_a != num_b) return num_a < num_b;
        } 
        // 否则按字符串比较
        else if (part_a != part_b) {
            return part_a < part_b;
        }
    }
    
    // 处理剩余部分
    return (it_a == end && it_b != end) || a < b;
}

// 获取目录下所有文件（按自然顺序排序）
std::vector<fs::path> get_sorted_files(const fs::path& dir_path) {
    std::vector<fs::path> files;
    
    // 遍历目录
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (fs::is_regular_file(entry.status())) {
            files.push_back(entry.path().filename());
        }
    }
    
    // 检测是否需要数字模式（检查是否有4位数字文件）
    bool use_numeric_mode = false;
    for (const auto& file : files) {
        std::string name = file.string();
        if (name.length() >= 8 && 
            std::regex_match(name, std::regex("\\d{4}\\.[a-zA-Z0-9]{3}"))) {
            use_numeric_mode = true;
            break;
        }
    }
    
    // 排序逻辑
    if (use_numeric_mode) {
        // 严格数字模式：0000~9999
        std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
            std::string sa = a.string();
            std::string sb = b.string();
            if (sa.length() < 8 || sb.length() < 8) return sa < sb;
            
            // 提取4位数字前缀
            int num_a = std::stoi(sa.substr(0, 4));
            int num_b = std::stoi(sb.substr(0, 4));
            return num_a < num_b;
        });
    } else {
        // 通用自然排序
        std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
            return natural_sort_compare(a.string(), b.string());
        });
    }
    
    return files;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_dir> <output.xfl>\n", argv[0]);
        return 1;
    }

    fs::path input_dir = argv[1];
    fs::path output_file = argv[2];

    // 验证输入目录
    if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
        fprintf(stderr, "Error: Input directory '%s' does not exist or is not a directory.\n", 
                input_dir.string().c_str());
        return 1;
    }

    // 获取排序后的文件列表
    std::vector<fs::path> sorted_files = get_sorted_files(input_dir);
    
    if (sorted_files.empty()) {
        fprintf(stderr, "No valid files found in directory.\n");
        return 1;
    }

    // 计算总大小
    size_t total_data_size = 0;
    std::vector<size_t> file_sizes;
    
    for (const auto& filename : sorted_files) {
        fs::path full_path = input_dir / filename;
        if (!fs::exists(full_path)) continue;
        
        uintmax_t size = fs::file_size(full_path);
        if (size > UINT32_MAX) {
            fprintf(stderr, "Warning: Skipping '%s' (too large: %zu bytes)\n", 
                    filename.string().c_str(), size);
            continue;
        }
        
        file_sizes.push_back(static_cast<size_t>(size));
        total_data_size += size;
    }

    if (file_sizes.empty()) {
        fprintf(stderr, "No valid files after size validation.\n");
        return 1;
    }

    ULONG chunk_count = static_cast<ULONG>(sorted_files.size());
    ULONG chunk_size = chunk_count * sizeof(ChunkItem);

    // 构造 Header
    Header header = {
        0x0001424c,    // Magic
        chunk_size,    // ChunkSize
        chunk_count    // ChunkCount
    };

    // 分配缓冲区
    std::vector<BYTE> chunk_data(chunk_size, 0);
    std::vector<BYTE> file_data(total_data_size, 0);

    // 填充 ChunkItems 和文件数据
    ULONG current_offset = 0;
    BYTE* data_ptr = file_data.data();

    for (size_t i = 0; i < sorted_files.size(); ++i) {
        fs::path filename = sorted_files[i];
        fs::path full_path = input_dir / filename;
        size_t file_size = file_sizes[i];

        // 填充 ChunkItem
        ChunkItem* item = reinterpret_cast<ChunkItem*>(chunk_data.data() + i * sizeof(ChunkItem));
        
        // 安全复制文件名（截断至31字符+1终止符）
        std::string name_str = filename.string();
        size_t copy_len = std::min(name_str.length(), static_cast<size_t>(0x1F));
        strncpy(item->FileName, name_str.c_str(), copy_len);
        item->FileName[copy_len] = '\0';
        
        item->Offset = current_offset;
        item->Size = static_cast<DWORD>(file_size);

        // 读取文件内容
        FILE* fp = fopen(full_path.string().c_str(), "rb");
        if (!fp) {
            fprintf(stderr, "Error: Failed to open '%s'\n", full_path.string().c_str());
            return 1;
        }

        if (fread(data_ptr, 1, file_size, fp) != file_size) {
            fclose(fp);
            fprintf(stderr, "Error: Incomplete read from '%s'\n", full_path.string().c_str());
            return 1;
        }
        fclose(fp);

        current_offset += static_cast<ULONG>(file_size);
        data_ptr += file_size;
    }

    // 写入输出文件
    FILE* fout = fopen(output_file.string().c_str(), "wb");
    if (!fout) {
        perror("Failed to create output file");
        return 1;
    }

    fwrite(&header, 1, sizeof(header), fout);
    fwrite(chunk_data.data(), 1, chunk_size, fout);
    fwrite(file_data.data(), 1, total_data_size, fout);
    fclose(fout);

    printf("Successfully packed %zu files in sorted order into '%s'.\n",
           sorted_files.size(), output_file.string().c_str());
    return 0;
}