#include "ResumeSessionHandler.h"

ResumeSessionHandler::ResumeSessionHandler(Session* session, CARTA::ResumeSession message, uint32_t request_id) {
    _session = session;
    _message = message;
    _request_id = request_id;
    Execute();
}

void ResumeSessionHandler::Execute() {
    bool success(true);
    // Error message
    std::string err_message;
    std::string err_file_ids = "Problem loading files: ";
    std::string err_region_ids = "Problem loading regions: ";

    // Close all images
    CARTA::CloseFile close_file_msg;
    close_file_msg.set_file_id(-1);
    CloseFileCmd(close_file_msg);

    // Open images
    for (int i = 0; i < _message.images_size(); ++i) {
        const CARTA::ImageProperties& image = _message.images(i);
        CARTA::OpenFile open_file_msg;
        open_file_msg.set_directory(image.directory());
        open_file_msg.set_file(image.file());
        open_file_msg.set_hdu(image.hdu());
        open_file_msg.set_file_id(image.file_id());
        if (!OpenFileCmd(open_file_msg)) {
            success = false;
            // Error message
            std::string file_id = std::to_string(image.file_id()) + " ";
            err_file_ids.append(file_id);
        }

        // Set image channels
        CARTA::SetImageChannels set_image_channels_msg;
        set_image_channels_msg.set_channel(image.channel());
        set_image_channels_msg.set_stokes(image.stokes());
        SetImageChannelsCmd(set_image_channels_msg);

        // Set regions
        for (int j = 0; j < image.regions_size(); ++j) {
            const CARTA::RegionProperties& region = image.regions(j);
            CARTA::SetRegion set_region_msg;
            set_region_msg.set_region_name(region.region_info().region_name());
            set_region_msg.set_region_id(region.region_id());
            set_region_msg.set_rotation(region.region_info().rotation());
            set_region_msg.set_file_id(i);
            set_region_msg.set_region_type(region.region_info().region_type());
            *set_region_msg.mutable_control_points() = {
                region.region_info().control_points().begin(), region.region_info().control_points().end()};
            if (!SetRegionCmd(set_region_msg)) {
                success = false;
                // Error message
                std::string region_id = std::to_string(region.region_id()) + " ";
                err_region_ids.append(region_id);
            }
        }
    }

    // RESPONSE
    CARTA::ResumeSessionAck ack;
    ack.set_success(success);
    if (!success) {
        err_message = err_file_ids + err_region_ids;
        ack.set_message(err_message);
    }
    _session->SendEvent(CARTA::EventType::RESUME_SESSION_ACK, _request_id, ack);
}

void ResumeSessionHandler::CloseFileCmd(CARTA::CloseFile message) {
    _session->CheckCancelAnimationOnFileClose(message.file_id());
    _session->_file_settings.ClearSettings(message.file_id());
    _session->OnCloseFile(message);
}

bool ResumeSessionHandler::OpenFileCmd(CARTA::OpenFile message) {
    return _session->OnOpenFile(message, _request_id);
}

void ResumeSessionHandler::SetImageChannelsCmd(CARTA::SetImageChannels message) {
    OnMessageTask* tsk = nullptr;
    _session->ImageChannelLock();
    if (!_session->ImageChannelTaskTestAndSet()) {
        tsk = new (tbb::task::allocate_root(_session->Context())) SetImageChannelsTask(_session);
    }
    // has its own queue to keep channels in order during animation
    _session->AddToSetChannelQueue(message, _request_id);
    _session->ImageChannelUnlock();
    tbb::task::enqueue(*tsk);
}

bool ResumeSessionHandler::SetRegionCmd(CARTA::SetRegion message) {
    return _session->OnSetRegion(message, _request_id);
}