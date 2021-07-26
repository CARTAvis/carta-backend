/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "CommonTestUtilities.h"
#include "DummyBackend.h"

using namespace std;

class ICDTest : public ::testing::Test, public FileFinder {
public:
    ICDTest() : _dummy_backend(std::make_unique<DummyBackend>()) {}

    ~ICDTest() = default;

    void TestAccessCarta(uint32_t session_id, string api_key, uint32_t client_feature_flags, CARTA::SessionType expected_session_type,
        bool expected_message) {
        CARTA::RegisterViewer register_viewer_msg;
        register_viewer_msg.set_session_id(session_id);
        register_viewer_msg.set_api_key(api_key);
        register_viewer_msg.set_client_feature_flags(client_feature_flags);

        auto t_start = std::chrono::high_resolution_clock::now();

        _dummy_backend->ReceiveMessage(register_viewer_msg);

        auto t_end = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        EXPECT_LT(dt, 100); // expect the process time within 100 ms

        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
                EXPECT_EQ(head.type, CARTA::EventType::REGISTER_VIEWER_ACK);

                if (head.type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                    CARTA::RegisterViewerAck register_viewer_ack;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    register_viewer_ack.ParseFromArray(event_buf, event_length);

                    EXPECT_TRUE(register_viewer_ack.success());
                    EXPECT_EQ(register_viewer_ack.session_id(), session_id);
                    EXPECT_EQ(register_viewer_ack.session_type(), expected_session_type);
                    EXPECT_EQ(register_viewer_ack.user_preferences_size(), 0);
                    EXPECT_EQ(register_viewer_ack.user_layouts_size(), 0);
                    if (expected_message) {
                        EXPECT_GT(register_viewer_ack.message().length(), 0);
                    } else {
                        EXPECT_EQ(register_viewer_ack.message().length(), 0);
                    }
                }
            }
        });
    }

    void TestAnimatorNavigation() {
        // check the existence of sample files
        string sample_file_name_str = Hdf5ImagePath("HH211_IQU.hdf5");
        fs::path sample_file_name(sample_file_name_str);
        if (!fs::exists(sample_file_name)) {
            spdlog::warn("File {} does not exist. Ignore the test.", sample_file_name_str);
            return;
        }

        string sample_file_name_str2 = Hdf5ImagePath("M17_SWex.hdf5");
        fs::path sample_file_name2(sample_file_name_str2);
        if (!fs::exists(sample_file_name2)) {
            spdlog::warn("File {} does not exist. Ignore the test.", sample_file_name_str2);
            return;
        }

        CARTA::RegisterViewer register_viewer_msg;
        register_viewer_msg.set_session_id(0);
        register_viewer_msg.set_api_key("");
        register_viewer_msg.set_client_feature_flags(5);

        _dummy_backend->ReceiveMessage(register_viewer_msg);
        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());
                EXPECT_EQ(head.type, CARTA::EventType::REGISTER_VIEWER_ACK);

                if (head.type == CARTA::EventType::REGISTER_VIEWER_ACK) {
                    CARTA::RegisterViewerAck register_viewer_ack;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    register_viewer_ack.ParseFromArray(event_buf, event_length);
                    EXPECT_TRUE(register_viewer_ack.success());
                }
            }
        });

        CARTA::CloseFile close_file_msg;
        close_file_msg.set_file_id(-1);

        _dummy_backend->ReceiveMessage(close_file_msg);

        CARTA::OpenFile open_file_msg;
        open_file_msg.set_directory(Hdf5ImagePath(""));
        open_file_msg.set_file("HH211_IQU.hdf5");
        open_file_msg.set_file_id(0);
        open_file_msg.set_hdu("0");
        open_file_msg.set_render_mode(CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file_msg);
        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());

                if (head.type == CARTA::EventType::OPEN_FILE_ACK) {
                    CARTA::OpenFileAck open_file_ack;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    open_file_ack.ParseFromArray(event_buf, event_length);
                    EXPECT_TRUE(open_file_ack.success());
                }

                if (head.type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                    CARTA::RegionHistogramData region_histogram_data;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    region_histogram_data.ParseFromArray(event_buf, event_length);
                    EXPECT_GE(region_histogram_data.histograms_size(), 0);
                }
            }
        });

        CARTA::SetImageChannels set_image_channels_msg;
        set_image_channels_msg.set_file_id(0);
        set_image_channels_msg.set_channel(0);
        set_image_channels_msg.set_stokes(0);
        CARTA::AddRequiredTiles* required_tiles = set_image_channels_msg.mutable_required_tiles();
        required_tiles->set_file_id(0);
        required_tiles->set_compression_quality(11);
        required_tiles->set_compression_type(CARTA::CompressionType::ZFP);
        required_tiles->add_tiles(0);

        _dummy_backend->ReceiveMessage(set_image_channels_msg);
        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());

                if (head.type == CARTA::EventType::RASTER_TILE_DATA) {
                    CARTA::RasterTileData raster_tile_data;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    raster_tile_data.ParseFromArray(event_buf, event_length);
                    EXPECT_EQ(raster_tile_data.file_id(), 0);
                    EXPECT_EQ(raster_tile_data.channel(), 0);
                    EXPECT_EQ(raster_tile_data.stokes(), 0);
                }
            }
        });

        CARTA::OpenFile open_file_msg2;
        open_file_msg2.set_directory(Hdf5ImagePath(""));
        open_file_msg2.set_file("M17_SWex.hdf5");
        open_file_msg2.set_file_id(1);
        open_file_msg2.set_hdu("0");
        open_file_msg2.set_render_mode(CARTA::RenderMode::RASTER);

        _dummy_backend->ReceiveMessage(open_file_msg2);
        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());

                if (head.type == CARTA::EventType::OPEN_FILE_ACK) {
                    CARTA::OpenFileAck open_file_ack;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    open_file_ack.ParseFromArray(event_buf, event_length);
                    EXPECT_TRUE(open_file_ack.success());
                }

                if (head.type == CARTA::EventType::REGION_HISTOGRAM_DATA) {
                    CARTA::RegionHistogramData region_histogram_data;
                    char* event_buf = message.data() + sizeof(carta::EventHeader);
                    int event_length = message.size() - sizeof(carta::EventHeader);
                    region_histogram_data.ParseFromArray(event_buf, event_length);
                    EXPECT_GE(region_histogram_data.histograms_size(), 0);
                }
            }
        });

        CARTA::SetImageChannels set_image_channels_msg2;
        set_image_channels_msg2.set_file_id(0);
        set_image_channels_msg2.set_channel(2);
        set_image_channels_msg2.set_stokes(1);
        CARTA::AddRequiredTiles* required_tiles2 = set_image_channels_msg2.mutable_required_tiles();
        required_tiles2->set_file_id(0);
        required_tiles2->set_compression_quality(11);
        required_tiles2->set_compression_type(CARTA::CompressionType::ZFP);
        required_tiles2->add_tiles(0);

        _dummy_backend->ReceiveMessage(set_image_channels_msg2);
        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            int count = 0;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());

                if (head.type == CARTA::EventType::RASTER_TILE_DATA) {
                    ++count;
                    if (count == 2) {
                        CARTA::RasterTileData raster_tile_data;
                        char* event_buf = message.data() + sizeof(carta::EventHeader);
                        int event_length = message.size() - sizeof(carta::EventHeader);
                        raster_tile_data.ParseFromArray(event_buf, event_length);
                        EXPECT_EQ(raster_tile_data.file_id(), 0);
                        EXPECT_EQ(raster_tile_data.channel(), 2);
                        EXPECT_EQ(raster_tile_data.stokes(), 1);
                    }
                }
            }
        });

        CARTA::SetImageChannels set_image_channels_msg3;
        set_image_channels_msg3.set_file_id(1);
        set_image_channels_msg3.set_channel(12);
        set_image_channels_msg3.set_stokes(0);
        CARTA::AddRequiredTiles* required_tiles3 = set_image_channels_msg3.mutable_required_tiles();
        required_tiles3->set_file_id(1);
        required_tiles3->set_compression_quality(11);
        required_tiles3->set_compression_type(CARTA::CompressionType::ZFP);
        required_tiles3->add_tiles(0);

        _dummy_backend->ReceiveMessage(set_image_channels_msg3);
        _dummy_backend->CheckMessagesQueue([=](tbb::concurrent_queue<std::pair<std::vector<char>, bool>> messages_queue) {
            std::pair<std::vector<char>, bool> messages_pair;
            int count = 0;
            while (messages_queue.try_pop(messages_pair)) {
                std::vector<char> message = messages_pair.first;
                carta::EventHeader head = *reinterpret_cast<const carta::EventHeader*>(message.data());

                if (head.type == CARTA::EventType::RASTER_TILE_DATA) {
                    ++count;
                    if (count == 3) {
                        CARTA::RasterTileData raster_tile_data;
                        char* event_buf = message.data() + sizeof(carta::EventHeader);
                        int event_length = message.size() - sizeof(carta::EventHeader);
                        raster_tile_data.ParseFromArray(event_buf, event_length);
                        EXPECT_EQ(raster_tile_data.file_id(), 1);
                        EXPECT_EQ(raster_tile_data.channel(), 12);
                        EXPECT_EQ(raster_tile_data.stokes(), 0);
                    }
                }
            }
        });
    }

private:
    std::unique_ptr<DummyBackend> _dummy_backend;
};

TEST_F(ICDTest, ACCESS_CARTA_DEFAULT) {
    TestAccessCarta(0, "", 5, CARTA::SessionType::NEW, true);
}

TEST_F(ICDTest, ACCESS_CARTA_KNOWN_DEFAULT) {
    TestAccessCarta(9999, "", 5, CARTA::SessionType::RESUMED, true);
}

TEST_F(ICDTest, ACCESS_CARTA_NO_CLIENT_FEATURE) {
    TestAccessCarta(0, "", 0, CARTA::SessionType::NEW, true);
}

TEST_F(ICDTest, ACCESS_CARTA_SAME_ID_TWICE) {
    TestAccessCarta(12345, "", 5, CARTA::SessionType::RESUMED, true);
    TestAccessCarta(12345, "", 5, CARTA::SessionType::RESUMED, true);
}

TEST_F(ICDTest, ANIMATOR_NAVIGATION) {
    TestAnimatorNavigation();
}
