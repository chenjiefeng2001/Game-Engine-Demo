#include <gtest/gtest.h>

#include "Engine/Core/Content/PrefabApplyRecord.h"
#include "Engine/Core/Content/Utf8Path.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Engine;
using namespace Engine::Content;

namespace {

class PrefabApplyRecordTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_RootPath = std::filesystem::temp_directory_path() /
                     ("engine_g6_apply_record_" + std::to_string(
                         static_cast<unsigned long long>(
                             std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count())));
        std::filesystem::create_directories(m_RootPath);
        PathError error = PathError::None;
        std::string diagnostic;
        ASSERT_TRUE(ProjectRoot::Create(m_RootPath.generic_string(), &m_Root,
                                        &error, &diagnostic)) << diagnostic;
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_RootPath, ec);
    }

    static PrefabApplyRecord PreparedRecord() {
        PrefabApplyRecord record;
        record.transactionId = "apply-0123456789";
        record.recordVersion = 1;
        record.state = PrefabApplyState::Prepared;
        record.prefabGuid = ResourceGUID::FromHex(
            "0123456789abcdef0123456789abcdef");
        record.oldSourceRevision = 1;
        record.oldSourceDigest = "0011223344556677";
        record.oldSourcePath = "assets/prefabs/a.0011223344556677.prefab.json";
        record.newDigest = "8899aabbccddeeff";
        record.newPath = "assets/prefabs/a.8899aabbccddeeff.prefab.json";
        record.newSourceRevision = -1;
        record.instanceRootEntityId = 21;
        record.oldInstanceMetadata = {{"sourceRevision", 1}};
        record.newInstanceMetadata = {{"sourceRevision", 2}};
        record.oldRevisionSnapshot = 1;
        record.phase1Revision = -1;
        record.phase2Revision = -1;
        return record;
    }

    static std::string ReadBytes(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
    }

    static void WriteBytes(const std::filesystem::path& path,
                           const std::string& bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << bytes;
    }

    ProjectRoot m_Root;
    std::filesystem::path m_RootPath;
};

} // namespace

TEST_F(PrefabApplyRecordTest, RoundTripAndAtomicUpdate) {
    PrefabApplyRecord record = PreparedRecord();
    std::string error;
    EXPECT_TRUE(ValidatePrefabApplyRecord(record, &error)) << error;
    EXPECT_TRUE(WritePrefabApplyRecord(m_Root, record, &error)) << error;

    PrefabApplyRecord loaded;
    EXPECT_TRUE(ReadPrefabApplyRecord(m_Root, record.prefabGuid,
                                      record.instanceRootEntityId, &loaded,
                                      &error)) << error;
    EXPECT_TRUE(loaded.integrityValid);
    EXPECT_EQ(loaded.transactionId, record.transactionId);
    EXPECT_EQ(loaded.prefabGuid, record.prefabGuid);
    EXPECT_EQ(loaded.state, PrefabApplyState::Prepared);
    EXPECT_EQ(loaded.oldSourceDigest, record.oldSourceDigest);
    EXPECT_EQ(loaded.newDigest, record.newDigest);
    EXPECT_EQ(loaded.instanceRootEntityId, 21u);

    record.state = PrefabApplyState::PrefabPublished;
    record.phase1Revision = 2;
    record.newSourceRevision = 2;
    EXPECT_TRUE(WritePrefabApplyRecord(m_Root, record, &error)) << error;
    EXPECT_TRUE(ReadPrefabApplyRecord(m_Root, record.prefabGuid,
                                      record.instanceRootEntityId, &loaded,
                                      &error)) << error;
    EXPECT_EQ(loaded.state, PrefabApplyState::PrefabPublished);
    EXPECT_EQ(loaded.phase1Revision, 2);
}

TEST_F(PrefabApplyRecordTest, StrictParserRejectsUnknownMissingAndCorruption) {
    PrefabApplyRecord record = PreparedRecord();
    std::string bytes;
    std::string error;
    ASSERT_TRUE(SerializePrefabApplyRecord(record, &bytes, &error)) << error;
    auto doc = nlohmann::json::parse(bytes);

    doc["unknown"] = 1;
    EXPECT_FALSE(ParsePrefabApplyRecord(doc.dump(), &record, &error));
    doc = nlohmann::json::parse(bytes);
    doc.erase("integrityHash");
    EXPECT_FALSE(ParsePrefabApplyRecord(doc.dump(), &record, &error));
    doc = nlohmann::json::parse(bytes);
    doc["recordVersion"] = 2;
    EXPECT_FALSE(ParsePrefabApplyRecord(doc.dump(), &record, &error));
    doc = nlohmann::json::parse(bytes);
    doc["state"] = "bogus";
    EXPECT_FALSE(ParsePrefabApplyRecord(doc.dump(), &record, &error));
    doc = nlohmann::json::parse(bytes);
    doc["integrityHash"] = "0000000000000000";
    EXPECT_FALSE(ParsePrefabApplyRecord(doc.dump(), &record, &error));
    EXPECT_FALSE(ParsePrefabApplyRecord("not json", &record, &error));
    EXPECT_FALSE(ParsePrefabApplyRecord(bytes + " trailing", &record, &error));
}

TEST_F(PrefabApplyRecordTest, RejectsInvalidStateIdentityAndPath) {
    PrefabApplyRecord record = PreparedRecord();
    std::string error;
    record.phase1Revision = 1;
    EXPECT_FALSE(ValidatePrefabApplyRecord(record, &error));
    EXPECT_NE(error.find("prepared"), std::string::npos) << error;

    record = PreparedRecord();
    record.newSourceRevision = -2;
    EXPECT_FALSE(ValidatePrefabApplyRecord(record, &error));
    record = PreparedRecord();
    record.oldSourcePath = "/abs/a.prefab.json";
    EXPECT_FALSE(ValidatePrefabApplyRecord(record, &error));
    record = PreparedRecord();
    record.newPath = "assets/prefabs/../a.prefab.json";
    EXPECT_FALSE(ValidatePrefabApplyRecord(record, &error));
    record = PreparedRecord();
    record.oldSourceDigest = "zzzz";
    EXPECT_FALSE(ValidatePrefabApplyRecord(record, &error));
    record = PreparedRecord();
    record.transactionId = "bad id";
    EXPECT_FALSE(ValidatePrefabApplyRecord(record, &error));
}

TEST_F(PrefabApplyRecordTest, AtomicWriteFailureAndRemoveFailsClosed) {
    PrefabApplyRecord record = PreparedRecord();
    std::string error;
    // Create a regular file where the recovery directory should be.
    const std::filesystem::path meta = m_RootPath / ".engine-content";
    WriteBytes(meta, "blocking");
    EXPECT_FALSE(WritePrefabApplyRecord(m_Root, record, &error));
    EXPECT_NE(error.find("directory"), std::string::npos) << error;

    std::error_code ec;
    std::filesystem::remove(meta, ec);
    ASSERT_FALSE(ec);
    EXPECT_TRUE(WritePrefabApplyRecord(m_Root, record, &error)) << error;
    EXPECT_TRUE(RemovePrefabApplyRecord(m_Root, record.prefabGuid,
                                        record.instanceRootEntityId, &error))
        << error;
    PrefabApplyRecord loaded;
    EXPECT_FALSE(ReadPrefabApplyRecord(m_Root, record.prefabGuid,
                                       record.instanceRootEntityId, &loaded,
                                       &error));
}