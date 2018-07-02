#include "AppController.hpp"

#include <future>
#include <sstream>
#include <cstdarg>
#include <thread>
#include <unordered_map>
#include <chrono>

#include <slic3r/GUI/GUI.hpp>
#include <slic3r/GUI/PresetBundle.hpp>

#include <PrintConfig.hpp>
#include <Print.hpp>
#include <PrintExport.hpp>
#include <Model.hpp>
#include <Utils.hpp>

namespace Slic3r {

class AppControllerBoilerplate::PriMap {
public:
    using M = std::unordered_map<std::thread::id, ProgresIndicatorPtr>;
    std::mutex m;
    M store;
    std::thread::id ui_thread;

    inline explicit PriMap(std::thread::id uit): ui_thread(uit) {}
};

AppControllerBoilerplate::AppControllerBoilerplate()
    :progressind_(new PriMap(std::this_thread::get_id())) {}

AppControllerBoilerplate::~AppControllerBoilerplate() {
    progressind_.reset();
}

bool AppControllerBoilerplate::is_main_thread() const
{
    return progressind_->ui_thread == std::this_thread::get_id();
}

namespace GUI {
PresetBundle* get_preset_bundle();
}

static const PrintObjectStep STEP_SLICE                 = posSlice;
static const PrintObjectStep STEP_PERIMETERS            = posPerimeters;
static const PrintObjectStep STEP_PREPARE_INFILL        = posPrepareInfill;
static const PrintObjectStep STEP_INFILL                = posInfill;
static const PrintObjectStep STEP_SUPPORTMATERIAL       = posSupportMaterial;
static const PrintStep STEP_SKIRT                       = psSkirt;
static const PrintStep STEP_BRIM                        = psBrim;
static const PrintStep STEP_WIPE_TOWER                  = psWipeTower;

void AppControllerBoilerplate::progress_indicator(
        AppControllerBoilerplate::ProgresIndicatorPtr progrind) {
    progressind_->m.lock();
    progressind_->store[std::this_thread::get_id()] = progrind;
    progressind_->m.unlock();
}

void AppControllerBoilerplate::progress_indicator(unsigned statenum,
                                                  const string &title,
                                                  const string &firstmsg)
{
    progressind_->m.lock();
    progressind_->store[std::this_thread::get_id()] =
            create_progress_indicator(statenum, title, firstmsg);
    progressind_->m.unlock();
}

void AppControllerBoilerplate::progress_indicator(unsigned statenum,
                                                  const string &title)
{
    progressind_->m.lock();
    progressind_->store[std::this_thread::get_id()] =
            create_progress_indicator(statenum, title);
    progressind_->m.unlock();
}

AppControllerBoilerplate::ProgresIndicatorPtr
AppControllerBoilerplate::progress_indicator() {

    PriMap::M::iterator pret;
    ProgresIndicatorPtr ret;

    progressind_->m.lock();
    if( (pret = progressind_->store.find(std::this_thread::get_id()))
            == progressind_->store.end())
    {
        progressind_->store[std::this_thread::get_id()] = ret =
                global_progressind_;
    } else ret = pret->second;
    progressind_->m.unlock();

    return ret;
}

void PrintController::make_skirt()
{
    assert(print_ != nullptr);

    // prerequisites
    for(auto obj : print_->objects) make_perimeters(obj);
    for(auto obj : print_->objects) infill(obj);
    for(auto obj : print_->objects) gen_support_material(obj);

    if(!print_->state.is_done(STEP_SKIRT)) {
        print_->state.set_started(STEP_SKIRT);
        print_->skirt.clear();
        if(print_->has_skirt()) print_->_make_skirt();

        print_->state.set_done(STEP_SKIRT);
    }
}

void PrintController::make_brim()
{
    assert(print_ != nullptr);

    // prerequisites
    for(auto obj : print_->objects) make_perimeters(obj);
    for(auto obj : print_->objects) infill(obj);
    for(auto obj : print_->objects) gen_support_material(obj);
    make_skirt();

    if(!print_->state.is_done(STEP_BRIM)) {
        print_->state.set_started(STEP_BRIM);

        // since this method must be idempotent, we clear brim paths *before*
        // checking whether we need to generate them
        print_->brim.clear();

        if(print_->config.brim_width > 0) print_->_make_brim();

        print_->state.set_done(STEP_BRIM);
    }
}

void PrintController::make_wipe_tower()
{
    assert(print_ != nullptr);

    // prerequisites
    for(auto obj : print_->objects) make_perimeters(obj);
    for(auto obj : print_->objects) infill(obj);
    for(auto obj : print_->objects) gen_support_material(obj);
    make_skirt();
    make_brim();

    if(!print_->state.is_done(STEP_WIPE_TOWER)) {
        print_->state.set_started(STEP_WIPE_TOWER);

        // since this method must be idempotent, we clear brim paths *before*
        // checking whether we need to generate them
        print_->brim.clear();

        if(print_->has_wipe_tower()) print_->_make_wipe_tower();

        print_->state.set_done(STEP_WIPE_TOWER);
    }
}

void PrintController::slice(PrintObject *pobj)
{
    assert(pobj != nullptr && print_ != nullptr);

    if(pobj->state.is_done(STEP_SLICE)) return;

    pobj->state.set_started(STEP_SLICE);

    pobj->_slice();

    auto msg = pobj->_fix_slicing_errors();
    if(!msg.empty()) report_issue(IssueType::WARN, msg);

    // simplify slices if required
    if (print_->config.resolution)
        pobj->_simplify_slices(scale_(print_->config.resolution));


    if(pobj->layers.empty())
        report_issue(IssueType::ERR,
                     _(L("No layers were detected. You might want to repair your "
                     "STL file(s) or check their size or thickness and retry"))
                     );

    pobj->state.set_done(STEP_SLICE);
}

void PrintController::make_perimeters(PrintObject *pobj)
{
    assert(pobj != nullptr);

    slice(pobj);

    auto&& prgind = progress_indicator();

    if (!pobj->state.is_done(STEP_PERIMETERS)) {
        pobj->_make_perimeters();
    }
}

void PrintController::infill(PrintObject *pobj)
{
    assert(pobj != nullptr);

    make_perimeters(pobj);

    if (!pobj->state.is_done(STEP_PREPARE_INFILL)) {
        pobj->state.set_started(STEP_PREPARE_INFILL);

        pobj->_prepare_infill();

        pobj->state.set_done(STEP_PREPARE_INFILL);
    }

    pobj->_infill();
}

void PrintController::gen_support_material(PrintObject *pobj)
{
    assert(pobj != nullptr);

    // prerequisites
    slice(pobj);

    if(!pobj->state.is_done(STEP_SUPPORTMATERIAL)) {
        pobj->state.set_started(STEP_SUPPORTMATERIAL);

        pobj->clear_support_layers();

        if((pobj->config.support_material || pobj->config.raft_layers > 0)
                && pobj->layers.size() > 1) {
            pobj->_generate_support_material();
        }

        pobj->state.set_done(STEP_SUPPORTMATERIAL);
    }
}

void PrintController::slice(AppControllerBoilerplate::ProgresIndicatorPtr pri)
{
    auto st = pri->state();

    Slic3r::trace(3, "Starting the slicing process.");

    pri->update(st+20, _(L("Generating perimeters")));
    for(auto obj : print_->objects) make_perimeters(obj);

    pri->update(st+60, _(L("Infilling layers")));
    for(auto obj : print_->objects) infill(obj);

    pri->update(st+70, _(L("Generating support material")));
    for(auto obj : print_->objects) gen_support_material(obj);

    pri->message_fmt(_(L("Weight: %.1fg, Cost: %.1f")),
                     print_->total_weight, print_->total_cost);
    pri->state(st+85);


    pri->update(st+88, _(L("Generating skirt")));
    make_skirt();


    pri->update(st+90, _(L("Generating brim")));
    make_brim();

    pri->update(st+95, _(L("Generating wipe tower")));
    make_wipe_tower();

    pri->update(st+100, _(L("Done")));

    // time to make some statistics..

    Slic3r::trace(3, _(L("Slicing process finished.")));
}


void PrintController::slice()
{
    auto pri = progress_indicator();
    slice(pri);
}

void PrintController::slice_to_png()
{
    auto exd = query_png_export_data();

    if(exd.zippath.empty()) return;

    auto presetbundle = GUI::get_preset_bundle();

    assert(presetbundle);

    auto conf = presetbundle->full_config();

    conf.validate();

    try {
        print_->apply_config(conf);
        print_->validate();
    } catch(std::exception& e) {
        report_issue(IssueType::ERR, e.what(), "Error");
        return;
    }

    // TODO: copy the model and work with the copy only
    bool correction = false;
    if(exd.corr_x != 1.0 || exd.corr_y != 1.0 || exd.corr_z != 1.0) {
        correction = true;
        print_->invalidate_all_steps();

        for(auto po : print_->objects) {
            po->model_object()->scale(
                        Pointf3(exd.corr_x, exd.corr_y, exd.corr_z)
                        );
            po->model_object()->invalidate_bounding_box();
            po->reload_model_instances();
            po->invalidate_all_steps();
        }
    }

    // Turn back the correction scaling on the model.
    auto scale_back = [&]() {
        if(correction) { // scale the model back
            print_->invalidate_all_steps();
            for(auto po : print_->objects) {
                po->model_object()->scale(
                    Pointf3(1.0/exd.corr_x, 1.0/exd.corr_y, 1.0/exd.corr_z)
                );
                po->model_object()->invalidate_bounding_box();
                po->reload_model_instances();
                po->invalidate_all_steps();
            }
        }
    };

    auto print_bb = print_->bounding_box();

    // If the print does not fit into the print area we should cry about it.
    if(unscale(print_bb.size().x) > exd.width_mm ||
            unscale(print_bb.size().y) > exd.height_mm) {
        std::stringstream ss;

        ss << _(L("Print will not fit and will be truncated!")) << "\n"
           << _(L("Width needed: ")) << unscale(print_bb.size().x) << " mm\n"
           << _(L("Height needed: ")) << unscale(print_bb.size().y) << " mm\n";

       if(!report_issue(IssueType::WARN_Q, ss.str(), _(L("Warning"))))  {
           scale_back();
           return;
       }
    }

    auto pri = create_progress_indicator(
                200, _(L("Slicing to zipped png files...")));

    try {
        pri->update(0, _(L("Slicing...")));
        slice(pri);
    } catch (std::exception& e) {
        pri->cancel();
        report_issue(IssueType::ERR, e.what(), _(L("Exception occured")));
        scale_back();
        return;
    }

    auto pbak = print_->progressindicator;
    print_->progressindicator = pri;

    try {
        print_to<FilePrinterFormat::PNG>( *print_, exd.zippath,
                    exd.width_mm, exd.height_mm,
                    exd.width_px, exd.height_px,
                    exd.exp_time_s, exd.exp_time_first_s);

    } catch (std::exception& e) {
        pri->cancel();
        print_->progressindicator = pbak;
        report_issue(IssueType::ERR, e.what(), _(L("Exception occured")));
    }

    scale_back();
}

void IProgressIndicator::message_fmt(
        const string &fmtstr, ...) {
    std::stringstream ss;
    va_list args;
    va_start(args, fmtstr);

    auto fmt = fmtstr.begin();

    while (*fmt != '\0') {
        if (*fmt == 'd') {
            int i = va_arg(args, int);
            ss << i << '\n';
        } else if (*fmt == 'c') {
            // note automatic conversion to integral type
            int c = va_arg(args, int);
            ss << static_cast<char>(c) << '\n';
        } else if (*fmt == 'f') {
            double d = va_arg(args, double);
            ss << d << '\n';
        }
        ++fmt;
    }

    va_end(args);
    message(ss.str());
}

}
