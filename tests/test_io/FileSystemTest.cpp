// P1: IO/Config 子系统测试 —— FileSystem
//
// FileSystem（185 行头 + 419 行实现）此前无任何直接测试。它是静态工具类，
// 全部基于 std::filesystem，无设备依赖。
//
// 断言分两类：
//  1. 纯路径函数（无 IO，可精确断言）
//  2. 真实文件 IO（在临时目录内做往返）
//
// 注意 API 实况（读源码确认，未猜测）：
//   - GetExtension 会**去掉前导点**："a/b.txt" → "txt"（不是 ".txt"）
//   - GetStem 用 fs::path::stem："archive.tar.gz" → "archive.tar"
//   - Normalize = lexically_normal()（纯词法，不访问磁盘）
//   - GetAbsolute = absolute(...).lexically_normal()
//   - Combine = fs::path(a) / fs::path(b)
//   - GetAppDirectory 返回 fs::current_path()
//   - GetDirectory 对无父路径的输入返回 ""（不是 "."）
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Engine/Core/FileSystem.h"

using namespace Engine;

namespace {

// 每个用例独享一个临时目录，析构时清理。
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        const auto base = std::filesystem::temp_directory_path() / "engine_fs_tests";
        m_Path = (base / ("dir" + std::to_string(counter++))).string();
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
        std::filesystem::create_directories(m_Path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
    }

    const std::string& path() const { return m_Path; }
    std::string file(const std::string& name) const { return m_Path + "/" + name; }

private:
    std::string m_Path;
};

void WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

} // namespace

// ── 生命周期 ───────────────────────────────────────────────────────────
TEST(FileSystemTest, InitIsIdempotentAndObservable)
{
    FileSystem::Shutdown();
    EXPECT_FALSE(FileSystem::IsInitialized());
    FileSystem::Init(2);
    EXPECT_TRUE(FileSystem::IsInitialized());
    FileSystem::Init(2);                     // 重复初始化必须安全
    EXPECT_TRUE(FileSystem::IsInitialized());
}

// ── 纯路径函数 ─────────────────────────────────────────────────────────
TEST(FileSystemTest, GetFileNameReturnsLastComponent)
{
    EXPECT_EQ(FileSystem::GetFileName("a/b/c.txt"), "c.txt");
    EXPECT_EQ(FileSystem::GetFileName("c.txt"), "c.txt");
    EXPECT_EQ(FileSystem::GetFileName("a/b/"), "");      // 以分隔符结尾
}

TEST(FileSystemTest, GetStemDropsExtensionOnly)
{
    EXPECT_EQ(FileSystem::GetStem("a/b/c.txt"), "c");
    // 多点扩展名只去最后一层
    EXPECT_EQ(FileSystem::GetStem("archive.tar.gz"), "archive.tar");
    EXPECT_EQ(FileSystem::GetStem("noext"), "noext");
}

TEST(FileSystemTest, GetExtensionStripsLeadingDot)
{
    // 实测：实现显式 erase 掉前导的 '.'
    EXPECT_EQ(FileSystem::GetExtension("a/b/c.txt"), "txt");
    EXPECT_EQ(FileSystem::GetExtension("archive.tar.gz"), "gz");
    EXPECT_EQ(FileSystem::GetExtension("noext"), "");
    EXPECT_EQ(FileSystem::GetExtension("a.b.c"), "c");
}

TEST(FileSystemTest, GetDirectoryReturnsParentOrEmpty)
{
    EXPECT_EQ(FileSystem::GetDirectory("a/b/c.txt"), "a/b");
    EXPECT_EQ(FileSystem::GetDirectory("c.txt"), "") << "无父路径应返回空串而非 \".\"";
}

TEST(FileSystemTest, CombineJoinsTwoComponents)
{
    const std::string joined = FileSystem::Combine("assets", "models/a.mdl");
    EXPECT_NE(joined.find("assets"), std::string::npos);
    EXPECT_NE(joined.find("a.mdl"), std::string::npos);
    EXPECT_EQ(FileSystem::GetFileName(joined), "a.mdl");
}

TEST(FileSystemTest, CombineWithEmptyBaseYieldsChild)
{
    const std::string joined = FileSystem::Combine("", "file.txt");
    EXPECT_EQ(FileSystem::GetFileName(joined), "file.txt");
}

TEST(FileSystemTest, NormalizeCollapsesDotSegments)
{
    const std::string n = FileSystem::Normalize("a/./b/../c.txt");
    // 结果应指向 a/c.txt："./" 与 "b/.." 都被消解
    EXPECT_EQ(FileSystem::GetFileName(n), "c.txt");
    EXPECT_EQ(n.find("b"), std::string::npos) << "\"b/..\" 未被消解";
    EXPECT_NE(n.find("a"), std::string::npos);
}

TEST(FileSystemTest, NormalizeIsPurelyLexicalAndDoesNotRequireExistence)
{
    // 路径完全不存在也必须正常返回，不得抛异常
    EXPECT_NO_THROW(FileSystem::Normalize("no/such/dir/../file.txt"));
}

TEST(FileSystemTest, IsAbsoluteDistinguishesDriveFromRelative)
{
    EXPECT_TRUE(FileSystem::IsAbsolute("C:/x/y.txt"));
    EXPECT_FALSE(FileSystem::IsAbsolute("x/y.txt"));
    EXPECT_FALSE(FileSystem::IsAbsolute("./x.txt"));
    EXPECT_FALSE(FileSystem::IsAbsolute(""));
}

TEST(FileSystemTest, GetAbsoluteMakesRelativePathAbsolute)
{
    const std::string abs = FileSystem::GetAbsolute("rel/file.txt");
    EXPECT_TRUE(FileSystem::IsAbsolute(abs)) << "GetAbsolute 未产生绝对路径";
    EXPECT_EQ(FileSystem::GetFileName(abs), "file.txt");
}

TEST(FileSystemTest, GetAppDirectoryIsNonEmpty)
{
    const std::string dir = FileSystem::GetAppDirectory();
    EXPECT_FALSE(dir.empty());
}

// ── 查询 ───────────────────────────────────────────────────────────────
TEST(FileSystemTest, ExistsAndKindChecksAgree)
{
    TempDir d;
    const auto f = d.file("data.bin");
    WriteBytes(f, {1, 2, 3});

    EXPECT_TRUE(FileSystem::Exists(f));
    EXPECT_TRUE(FileSystem::IsFile(f));
    EXPECT_FALSE(FileSystem::IsDirectory(f));

    EXPECT_TRUE(FileSystem::Exists(d.path()));
    EXPECT_TRUE(FileSystem::IsDirectory(d.path()));
    EXPECT_FALSE(FileSystem::IsFile(d.path()));
}

TEST(FileSystemTest, MissingPathsReportFalseNotCrash)
{
    const std::string missing = "definitely/not/here.bin";
    EXPECT_FALSE(FileSystem::Exists(missing));
    EXPECT_FALSE(FileSystem::IsFile(missing));
    EXPECT_FALSE(FileSystem::IsDirectory(missing));
}

TEST(FileSystemTest, GetFileSizeMatchesWrittenByteCount)
{
    TempDir d;
    const auto f = d.file("sized.bin");
    WriteBytes(f, {1, 2, 3, 4, 5});
    EXPECT_EQ(FileSystem::GetFileSize(f), 5u);

    WriteBytes(f, {});
    EXPECT_EQ(FileSystem::GetFileSize(f), 0u) << "空文件大小应为 0";
}

TEST(FileSystemTest, GetLastWriteTimeReturnsSaneValue)
{
    TempDir d;
    const auto f = d.file("stamped.txt");
    WriteBytes(f, {'x'});
    // 断言"合理"而非某个魔数：必须为正（现代纪元秒级时间戳）
    EXPECT_GT(FileSystem::GetLastWriteTime(f), 0);
}

// ── 读写往返 ───────────────────────────────────────────────────────────
TEST(FileSystemTest, BinaryWriteReadRoundTripPreservesBytes)
{
    TempDir d;
    const auto f = d.file("bytes.bin");
    const std::vector<uint8_t> payload = {0x00, 0xFF, 0x7F, 0x80, 0x00, 0x42};

    ASSERT_TRUE(FileSystem::WriteFile(f, payload));
    ASSERT_TRUE(FileSystem::IsFile(f));

    const std::vector<uint8_t> got = FileSystem::ReadFile(f);
    ASSERT_EQ(got.size(), payload.size()) << "往返字节数不一致";
    EXPECT_EQ(got, payload) << "往返内容不一致（含 NUL 与高位字节）";
}

TEST(FileSystemTest, ReadFileOfMissingPathReturnsEmpty)
{
    const auto got = FileSystem::ReadFile("no/such/file.bin");
    EXPECT_TRUE(got.empty()) << "读取缺失文件应返回空而非崩溃";
}

TEST(FileSystemTest, WriteFileCreatesMissingParentDirectories)
{
    TempDir d;
    const auto nested = d.file("deep/nested/dir/file.bin");
    const std::vector<uint8_t> payload = {7, 7, 7};

    ASSERT_TRUE(FileSystem::WriteFile(nested, payload))
        << "WriteFile 未能创建父目录";
    EXPECT_TRUE(FileSystem::IsFile(nested));
    EXPECT_EQ(FileSystem::ReadFile(nested).size(), 3u);
}

TEST(FileSystemTest, TextRoundTripPreservesUtf8)
{
    TempDir d;
    const auto f = d.file("utf8.txt");
    // 用显式 UTF-8 字节转义而非 u8"" 前缀：后者在 C++20 下是 const char8_t[]，
// 无法隐式转换为 std::string。转义写法也与源文件编码无关。
const std::string text = "ascii + \xE4\xB8\xAD\xE6\x96\x87 + \xC3\xA9\xC3\xA8";

    ASSERT_TRUE(FileSystem::WriteTextFile(f, text));
    EXPECT_EQ(FileSystem::ReadTextFile(f), text) << "UTF-8 文本往返被破坏";
}

TEST(FileSystemTest, TextRoundTripPreservesLfExactly)
{
    // K5 回归：文本往返必须逐字节无损。
    //
    // 实现不对称：WriteTextFile 用 `std::ofstream file{p}`（**文本**模式），
    // 而 ReadTextFile 走的是二进制的 ReadFile（FileSystem.cpp:353-376）。
    // Windows 上文本模式会把每个裸 \n 翻译成 \r\n，读回时无人还原，
    // 于是 round-trip 凭空多出 \r —— 实测
    //     写入 "line1\nline2\r\nline3"
    //     读出 "line1\r\nline2\r\r\nline3"
    // 该缺陷在 Linux 上完全不可见，属于典型的"CI 通过、Windows 出错"倒置，
    // 且会破坏 .sh / LF-only 文本。
    TempDir d;
    const auto f = d.file("lf.txt");
    ASSERT_TRUE(FileSystem::WriteTextFile(f, "line1\nline2\r\nline3"));
    EXPECT_EQ(FileSystem::ReadTextFile(f), "line1\nline2\r\nline3")
        << "K5: 文本往返改变了换行字节（写用文本模式、读用二进制模式）";
}

TEST(FileSystemTest, WriteTextFileUsesBinaryModeLikeWriteFile)
{
    // 同一断言的最小形态：单个裸 \n 写入后必须原样读回
    TempDir d;
    const auto f = d.file("single_lf.txt");
    ASSERT_TRUE(FileSystem::WriteTextFile(f, "a\nb"));
    const std::string got = FileSystem::ReadTextFile(f);
    EXPECT_EQ(got.size(), 3u) << "K5: 期望 3 字节，实际 " << got.size();
    EXPECT_EQ(got, "a\nb");
}

TEST(FileSystemTest, TextRoundTripPreservesEmptyString)
{
    TempDir d;
    const auto f = d.file("empty.txt");
    ASSERT_TRUE(FileSystem::WriteTextFile(f, ""));
    EXPECT_EQ(FileSystem::ReadTextFile(f), "");
}

TEST(FileSystemTest, AppendTextFileAccumulates)
{
    TempDir d;
    const auto f = d.file("append.txt");

    ASSERT_TRUE(FileSystem::WriteTextFile(f, "A"));
    ASSERT_TRUE(FileSystem::AppendTextFile(f, "B"));
    ASSERT_TRUE(FileSystem::AppendTextFile(f, "C"));

    EXPECT_EQ(FileSystem::ReadTextFile(f), "ABC") << "追加未累积";
}

TEST(FileSystemTest, AppendToMissingFileCreatesIt)
{
    TempDir d;
    const auto f = d.file("fresh_append.txt");
    ASSERT_TRUE(FileSystem::AppendTextFile(f, "hello"));
    EXPECT_TRUE(FileSystem::IsFile(f));
    EXPECT_EQ(FileSystem::ReadTextFile(f), "hello");
}

TEST(FileSystemTest, OverwritingTruncatesPreviousContent)
{
    TempDir d;
    const auto f = d.file("trunc.bin");
    ASSERT_TRUE(FileSystem::WriteFile(f, {1, 1, 1, 1, 1, 1, 1, 1}));
    ASSERT_TRUE(FileSystem::WriteFile(f, {9}));

    const auto got = FileSystem::ReadFile(f);
    ASSERT_EQ(got.size(), 1u) << "覆写未截断旧内容";
    EXPECT_EQ(got[0], 9);
}

// ── 目录与文件操作 ─────────────────────────────────────────────────────
TEST(FileSystemTest, CreateDirectoryIsRecursiveAndIdempotent)
{
    TempDir d;
    const auto nested = d.file("a/b/c");
    EXPECT_TRUE(FileSystem::CreateDirectory(nested)) << "首次创建应成功";
    EXPECT_TRUE(FileSystem::IsDirectory(nested));

    // 实测：实现直接返回 fs::create_directories(p)，该值在"目录已存在"时
    // 为 false（含义是"没有新建任何目录"）。因此返回值把
    // "做了工作" 与 "结果正确" 混为一谈。两种解读都站得住
    // （幂等成功 vs. 幂等无操作），API 无法判定作者意图，
    // 故此处只固定实测行为，不将其升格为缺陷。
    EXPECT_FALSE(FileSystem::CreateDirectory(nested))
        << "重复创建的返回值语义已变更：若改为幂等返回 true，此处需更新";
    EXPECT_TRUE(FileSystem::IsDirectory(nested)) << "重复创建破坏了已有目录";
}

TEST(FileSystemTest, RemoveDeletesFileOnly)
{
    TempDir d;
    const auto f = d.file("gone.txt");
    WriteBytes(f, {'x'});

    EXPECT_TRUE(FileSystem::Remove(f));
    EXPECT_FALSE(FileSystem::Exists(f));
}

TEST(FileSystemTest, RemoveAllDeletesNonEmptyTree)
{
    TempDir d;
    FileSystem::CreateDirectory(d.file("tree/sub"));
    WriteBytes(d.file("tree/a.txt"), {'a'});
    WriteBytes(d.file("tree/sub/b.txt"), {'b'});

    EXPECT_TRUE(FileSystem::RemoveAll(d.file("tree")));
    EXPECT_FALSE(FileSystem::Exists(d.file("tree")));
}

TEST(FileSystemTest, RenameMovesFile)
{
    TempDir d;
    const auto from = d.file("before.txt");
    const auto to = d.file("after.txt");
    WriteBytes(from, {'z'});

    EXPECT_TRUE(FileSystem::Rename(from, to));
    EXPECT_FALSE(FileSystem::Exists(from));
    ASSERT_TRUE(FileSystem::Exists(to));
    EXPECT_EQ(FileSystem::ReadTextFile(to), "z") << "重命名后内容丢失";
}

TEST(FileSystemTest, CopyDuplicatesContentAndKeepsOriginal)
{
    TempDir d;
    const auto from = d.file("src.txt");
    const auto to = d.file("dst.txt");
    WriteBytes(from, {'h', 'i'});

    EXPECT_TRUE(FileSystem::Copy(from, to));
    EXPECT_TRUE(FileSystem::Exists(from)) << "Copy 不应删除源文件";
    ASSERT_TRUE(FileSystem::Exists(to));
    EXPECT_EQ(FileSystem::ReadTextFile(to), "hi");
}

TEST(FileSystemTest, OperationsOnMissingPathsFailGracefully)
{
    EXPECT_FALSE(FileSystem::Remove("no/such/file.txt"));
    EXPECT_NO_THROW(FileSystem::RemoveAll("no/such/tree"));
    EXPECT_FALSE(FileSystem::Rename("no/such/a", "no/such/b"));
    EXPECT_FALSE(FileSystem::Copy("no/such/a", "no/such/b"));
}

// ── 目录扫描 ───────────────────────────────────────────────────────────
TEST(FileSystemTest, ScanDirectoryListsFilesAndSubdirs)
{
    TempDir d;
    WriteBytes(d.file("a.txt"), {'a'});
    WriteBytes(d.file("b.md"), {'b'});
    FileSystem::CreateDirectory(d.file("sub"));

    const auto entries = FileSystem::ScanDirectory(d.path());
    EXPECT_GE(entries.size(), 3u) << "应列出 2 个文件 + 1 个子目录";

    int dirs = 0, files = 0;
    for (const auto& e : entries) {
        EXPECT_FALSE(e.name.empty()) << "条目缺少 name";
        if (e.isDirectory) ++dirs; else ++files;
    }
    EXPECT_EQ(dirs, 1);
    EXPECT_GE(files, 2);
}

TEST(FileSystemTest, ScanFilesExcludesDirectories)
{
    TempDir d;
    WriteBytes(d.file("a.txt"), {'a'});
    FileSystem::CreateDirectory(d.file("subdir"));

    const auto files = FileSystem::ScanFiles(d.path());
    for (const auto& e : files) {
        EXPECT_FALSE(e.isDirectory) << "ScanFiles 不得返回目录条目";
    }
    EXPECT_GE(files.size(), 1u);
}

TEST(FileSystemTest, ScanDirectoryReportsSizeForFiles)
{
    TempDir d;
    WriteBytes(d.file("sized.bin"), {1, 2, 3, 4});

    const auto entries = FileSystem::ScanFiles(d.path());
    ASSERT_FALSE(entries.empty());
    for (const auto& e : entries) {
        if (e.name == "sized.bin") {
            EXPECT_EQ(e.size, 4u) << "条目 size 字段与实际不符";
            EXPECT_EQ(e.extension, "bin") << "条目 extension 未去掉前导点";
        }
    }
}

TEST(FileSystemTest, ScanMissingDirectoryReturnsEmpty)
{
    const auto entries = FileSystem::ScanDirectory("no/such/dir");
    EXPECT_TRUE(entries.empty()) << "扫描缺失目录应返回空而非崩溃";
}

TEST(FileSystemTest, RecursiveScanFindsNestedFiles)
{
    TempDir d;
    FileSystem::CreateDirectory(d.file("r/sub"));
    WriteBytes(d.file("r/top.txt"), {'t'});
    WriteBytes(d.file("r/sub/deep.txt"), {'d'});

    const auto shallow = FileSystem::ScanFiles(d.path(), "*", false);
    const auto deep = FileSystem::ScanFiles(d.path(), "*", true);
    EXPECT_GE(deep.size(), shallow.size()) << "递归扫描结果不应少于非递归";
}
