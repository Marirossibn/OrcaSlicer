#include "PrintJob.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "bambu_networking.hpp"

namespace Slic3r {
namespace GUI {

static wxString check_gcode_failed_str      = _L("Abnormal print file data. Please slice again.");
static wxString printjob_cancel_str         = _L("Task canceled.");
static wxString timeout_to_upload_str       = _L("Upload task timed out. Please check the network status and try again.");
static wxString failed_in_cloud_service_str = _L("Cloud service connection failed. Please try again.");
static wxString file_is_not_exists_str      = _L("Print file not found. please slice again.");
static wxString file_over_size_str          = _L("The print file exceeds the maximum allowable size (1GB). Please simplify the model and slice again.");
static wxString print_canceled_str          = _L("Task canceled.");
static wxString send_print_failed_str       = _L("Failed to send the print job. Please try again.");
static wxString upload_ftp_failed_str       = _L("Failed to upload file to ftp. Please try again.");

static wxString desc_network_error          = _L("Check the current status of the bambu server by clicking on the link above.");
static wxString desc_file_too_large         = _L("The size of the print file is too large. Please adjust the file size and try again.");
static wxString desc_fail_not_exist         = _L("Print file not found, Please slice it again and send it for printing.");
static wxString desc_upload_ftp_failed      = _L("Failed to upload print file to FTP. Please check the network status and try again.");

static wxString sending_over_lan_str        = _L("Sending print job over LAN");
static wxString sending_over_cloud_str      = _L("Sending print job through cloud service");

PrintJob::PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater, std::string dev_id)
: PlaterJob{ std::move(pri), plater },
    m_dev_id(dev_id)
{
    m_print_job_completed_id = plater->get_print_finished_event();
}

void PrintJob::prepare()
{
    m_plater->get_print_job_data(&job_data);
    if (&job_data) {
        std::string temp_file = Slic3r::resources_dir() + "/check_access_code.txt";
        auto check_access_code_path = temp_file.c_str();
        BOOST_LOG_TRIVIAL(trace) << "sned_job: check_access_code_path = " << check_access_code_path;
        job_data._temp_path = fs::path(check_access_code_path);
    }
}

void PrintJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        PlaterJob::on_exception(eptr);
    }
}

void PrintJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

wxString PrintJob::get_http_error_msg(unsigned int status, std::string body)
{
    try {
        int code = 0;
        std::string error;
        std::string message;
        wxString result;
        if (status >= 400 && status < 500)
            try {
            json j = json::parse(body);
            if (j.contains("code")) {
                if (!j["code"].is_null())
                    code = j["code"].get<int>();
            }
            if (j.contains("error")) {
                if (!j["error"].is_null())
                    error = j["error"].get<std::string>();
            }
            if (j.contains("message")) {
                if (!j["message"].is_null())
                    message = j["message"].get<std::string>();
            }
            switch (status) {
                ;
            }
        }
        catch (...) {
            ;
        }
        else if (status == 503) {
            return _L("Service Unavailable");
        }
        else {
            wxString unkown_text = _L("Unkown Error.");
            unkown_text += wxString::Format("status=%u, body=%s", status, body);
            BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;
            return unkown_text;
        }

        BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

        result = wxString::Format("code=%u, error=%s", code, from_u8(error));
        return result;
    } catch(...) {
        ;
    }
    return wxEmptyString;
} 

void PrintJob::process()
{
    /* display info */
    wxString msg;
    wxString error_str;
    int curr_percent = 10;
    NetworkAgent* m_agent = wxGetApp().getAgent();
    AppConfig* config = wxGetApp().app_config;

    if (this->connection_type == "lan") {
        msg = _L("Sending print job over LAN");
    }
    else {
        msg = _L("Sending print job through cloud service");
    }

    int result = -1;
    unsigned int http_code;
    std::string http_body;

    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        plate = m_plater->get_partplate_list().get_curr_plate();
        if (plate == nullptr)
        return;
    }

    /* check gcode is valid */
    if (!plate->is_valid_gcode_file() && m_print_type == "from_normal") {
        update_status(curr_percent, check_gcode_failed_str);
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    // task name
    std::string project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    int curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    else if (job_data.plate_idx == PLATE_ALL_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    else
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

    PartPlate* curr_plate = m_plater->get_partplate_list().get_curr_plate();
    if (curr_plate) {
        this->task_bed_type = bed_type_to_gcode_string(curr_plate->get_bed_type(true));
    }

    BBL::PrintParams params;

    // local print access
    params.dev_ip = m_dev_ip;
    params.use_ssl_for_ftp  = m_local_use_ssl_for_ftp;
    params.use_ssl_for_mqtt  = m_local_use_ssl_for_mqtt;
    params.username = "bblp";
    params.password = m_access_code;

    // check access code and ip address
    if (this->connection_type == "lan") {
        params.dev_id = m_dev_id;
        params.project_name = "verify_job";
        params.filename = job_data._temp_path.string();
        params.connection_type = this->connection_type;

        result = m_agent->start_send_gcode_to_sdcard(params, nullptr, nullptr);
        if (result != 0) {
            BOOST_LOG_TRIVIAL(error) << "access code is invalid";
            m_enter_ip_address_fun_fail();
            m_job_finished = true;
            return;
        }

        params.project_name = "";
        params.filename = "";
    }

    params.dev_id = m_dev_id;
    params.ftp_folder = m_ftp_folder;
    //params.project_name = m_project_name;
    
    
    params.filename = job_data._3mf_path.string();
    params.config_filename = job_data._3mf_config_path.string();
    params.plate_index = curr_plate_idx;
    params.task_bed_leveling    = this->task_bed_leveling;
    params.task_flow_cali       = this->task_flow_cali;
    params.task_vibration_cali  = this->task_vibration_cali;
    params.task_layer_inspect   = this->task_layer_inspect;
    params.task_record_timelapse= this->task_record_timelapse;
    params.ams_mapping          = this->task_ams_mapping;
    params.ams_mapping_info     = this->task_ams_mapping_info;
    params.connection_type      = this->connection_type;
    params.task_use_ams         = this->task_use_ams;
    params.task_bed_type        = this->task_bed_type;
    params.print_type            = this->m_print_type;

    if (wxGetApp().model().model_info && wxGetApp().model().model_info.get()) {
        ModelInfo* model_info = wxGetApp().model().model_info.get();
        auto origin_profile_id = model_info->metadata_items.find(BBL_DESIGNER_PROFILE_ID_TAG);
        if (origin_profile_id != model_info->metadata_items.end()) {
            try {
                params.origin_profile_id    = stoi(origin_profile_id->second.c_str());
            }
            catch(...) {}
        }
        auto origin_model_id = model_info->metadata_items.find(BBL_DESIGNER_MODEL_ID_TAG);
        if (origin_model_id != model_info->metadata_items.end()) {
            try {
                params.origin_model_id = origin_model_id->second;
            }
            catch(...) {}
        }

        auto profile_name = model_info->metadata_items.find(BBL_DESIGNER_PROFILE_TITLE_TAG);
        if (profile_name != model_info->metadata_items.end()) {
            try {
                params.preset_name = profile_name->second;
            }
            catch (...) {}
        } 
        
        auto model_name = model_info->metadata_items.find(BBL_DESIGNER_MODEL_TITLE_TAG);
        if (model_name != model_info->metadata_items.end()) {
            try {
                params.project_name = model_name->second;
            }
            catch (...) {}
        }
    }
    

    if (params.preset_name.empty()) {params.preset_name = wxString::Format("%s_plate_%d", m_project_name, curr_plate_idx).ToStdString();}
    if (params.project_name.empty()) {params.project_name = m_project_name;}

    wxString error_text;
    wxString msg_text;


    const int StagePercentPoint[(int)PrintingStageFinished + 1] = {
        20,     // PrintingStageCreate
        30,     // PrintingStageUpload
        70,     // PrintingStageWaiting
        75,     // PrintingStageRecord
        97,     // PrintingStageSending
        100     // PrintingStageFinished
    };

    bool is_try_lan_mode = false;
    bool is_try_lan_mode_failed = false;

    auto update_fn = [this, 
        &is_try_lan_mode,
        &is_try_lan_mode_failed,
        &msg, 
        &error_str, 
        &curr_percent, 
        &error_text,
        StagePercentPoint
    ](int stage, int code, std::string info) {

                        if (stage == BBL::SendingPrintJobStage::PrintingStageCreate && !is_try_lan_mode_failed) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload && !is_try_lan_mode_failed) {
                            if (code >= 0 && code <= 100 && !info.empty()) {
                                if (this->connection_type == "lan") {
                                    msg = _L("Sending print job over LAN");
                                } else {
                                    msg = _L("Sending print job through cloud service");
                                }
                                msg += wxString::Format("(%s)", info);
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageWaiting) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else  if (stage == BBL::SendingPrintJobStage::PrintingStageRecord && !is_try_lan_mode) {
                            msg = _L("Sending print configuration");
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageSending && !is_try_lan_mode) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                            msg = wxString::Format(_L("Successfully sent. Will automatically jump to the device page in %ss"), info);
                            this->update_percent_finish();
                        } else {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }

                        // update current percnet
                        if (stage >= 0 && stage <= (int) PrintingStageFinished) {
                            curr_percent = StagePercentPoint[stage];
                            if ((stage == BBL::SendingPrintJobStage::PrintingStageUpload
                                || stage == BBL::SendingPrintJobStage::PrintingStageRecord)
                                && (code > 0 && code <= 100)) {
                                curr_percent = (StagePercentPoint[stage + 1] - StagePercentPoint[stage]) * code / 100 + StagePercentPoint[stage];
                            }
                        }

                        //get errors 
                        if (code > 100 || code < 0 || stage == BBL::SendingPrintJobStage::PrintingStageERROR) {
                            if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE) {
                                m_plater->update_print_error_info(code, desc_file_too_large.ToStdString(), info);
                            }else if (code == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || code == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST){
                                m_plater->update_print_error_info(code, desc_fail_not_exist.ToStdString(), info);
                            }else if (code == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || code == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
                                m_plater->update_print_error_info(code, desc_upload_ftp_failed.ToStdString(), info);
                            }else {
                                m_plater->update_print_error_info(code, desc_network_error.ToStdString(), info);
                            }
                        }
                        else {
                             this->update_status(curr_percent, msg);
                        }
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };

    if (params.connection_type != "lan") {
        if (params.dev_ip.empty())
            params.comments = "no_ip";
        else if (this->cloud_print_only)
            params.comments = "low_version";
        else if (!this->has_sdcard)
            params.comments = "no_sdcard";
        else if (params.password.empty())
            params.comments = "no_password";


        //use ftp only
        if (!wxGetApp().app_config->get("lan_mode_only").empty() && wxGetApp().app_config->get("lan_mode_only") == "1") {

            if (params.password.empty() || params.dev_ip.empty()) {
                error_text = wxString::Format("Access code:%s Ip address:%s", params.password, params.dev_ip);
                result = BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "print_job: use ftp send print only";
                this->update_status(curr_percent, _L("Sending print job over LAN"));
                is_try_lan_mode = true;
                result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn);
                if (result < 0) {
                    error_text = wxString::Format("Access code:%s Ip address:%s", params.password, params.dev_ip);
                    // try to send with cloud
                    BOOST_LOG_TRIVIAL(warning) << "print_job: use ftp send print failed";
                }
            }
        }
        else {
            if (!this->cloud_print_only
                && !params.password.empty()
                && !params.dev_ip.empty()
                && this->has_sdcard) {
                // try to send local with record
                BOOST_LOG_TRIVIAL(info) << "print_job: try to start local print with record";
                this->update_status(curr_percent, _L("Sending print job over LAN"));
                result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn);
                if (result == 0) {
                    params.comments = "";
                }
                else if (result == BAMBU_NETWORK_ERR_PRINT_WR_UPLOAD_FTP_FAILED) {
                    params.comments = "upload_failed";
                }
                else {
                    params.comments = (boost::format("failed(%1%)") % result).str();
                }
                if (result < 0) {
                    is_try_lan_mode_failed = true;
                    // try to send with cloud
                    BOOST_LOG_TRIVIAL(warning) << "print_job: try to send with cloud";
                    this->update_status(curr_percent, _L("Sending print job through cloud service"));
                    result = m_agent->start_print(params, update_fn, cancel_fn);
                }
            }
            else {
                BOOST_LOG_TRIVIAL(info) << "print_job: send with cloud";
                this->update_status(curr_percent, _L("Sending print job through cloud service"));
                result = m_agent->start_print(params, update_fn, cancel_fn);
            }
        } 
    } else {
        if (this->has_sdcard) {
            this->update_status(curr_percent, _L("Sending print job over LAN"));
            result = m_agent->start_local_print(params, update_fn, cancel_fn);
        } else {
            this->update_status(curr_percent, _L("An SD card needs to be inserted before printing via LAN."));
            return;
        }
    }

    if (result < 0) {
        curr_percent = -1;

        if (result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_NOT_EXIST || result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_NOT_EXIST) {
            msg_text = file_is_not_exists_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_SP_FILE_OVER_SIZE || result == BAMBU_NETWORK_ERR_PRINT_WR_FILE_OVER_SIZE) {
            msg_text = file_over_size_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_CHECK_MD5_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SP_CHECK_MD5_FAILED) {
            msg_text = failed_in_cloud_service_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_WR_GET_NOTIFICATION_TIMEOUT || result == BAMBU_NETWORK_ERR_PRINT_SP_GET_NOTIFICATION_TIMEOUT) {
            msg_text = timeout_to_upload_str;
        } else if (result == BAMBU_NETWORK_ERR_PRINT_LP_UPLOAD_FTP_FAILED || result == BAMBU_NETWORK_ERR_PRINT_SG_UPLOAD_FTP_FAILED) {
            msg_text = upload_ftp_failed_str;
        } else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            msg_text = print_canceled_str;
        } else {
            msg_text = send_print_failed_str;
        }

        this->show_error_info(msg_text, 0, "", "");
        BOOST_LOG_TRIVIAL(error) << "print_job: failed, result = " << result;

    } else {
        BOOST_LOG_TRIVIAL(error) << "print_job: send ok.";
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        evt->SetString(m_dev_id);
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

void PrintJob::set_project_name(std::string name)
{
    m_project_name = name;
}

void PrintJob::on_check_ip_address_fail(std::function<void()> func)
{
    m_enter_ip_address_fun_fail = func;
}

void PrintJob::on_check_ip_address_success(std::function<void()> func)
{
    m_enter_ip_address_fun_success = func;
}

}} // namespace Slic3r::GUI
