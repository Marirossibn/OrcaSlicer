#include "GLGizmoMmuSegmentation.hpp"

#include "libslic3r/Model.hpp"

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/BitmapCache.hpp"
#include "libslic3r/PresetBundle.hpp"


#include <GL/glew.h>

namespace Slic3r::GUI {

void GLGizmoMmuSegmentation::on_shutdown()
{
//    m_seed_fill_angle = 0.f;
//    m_seed_fill_enabled = false;
    m_parent.use_slope(false);
}

std::string GLGizmoMmuSegmentation::on_get_name() const
{
    // FIXME Lukas H.: Discuss and change shortcut
    return (_L("MMU painting") + " [N]").ToUTF8().data();
}

bool GLGizmoMmuSegmentation::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
            && wxGetApp().get_mode() != comSimple && wxGetApp().extruders_cnt() > 1);
}

static std::vector<std::array<uint8_t, 3>> get_extruders_colors()
{
    unsigned char                       rgb_color[3] = {};
    std::vector<std::string>            colors       = Slic3r::GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();
    std::vector<std::array<uint8_t, 3>> colors_out(colors.size());
    for (const std::string &color : colors) {
        Slic3r::GUI::BitmapCache::parse_color(color, rgb_color);
        size_t color_idx      = &color - &colors.front();
        colors_out[color_idx] = {rgb_color[0], rgb_color[1], rgb_color[2]};
    }

    return colors_out;
}

static std::vector<std::string> get_extruders_names()
{
    size_t                   extruders_count = wxGetApp().extruders_cnt();
    std::vector<std::string> extruders_out;
    extruders_out.reserve(extruders_count);
    for (size_t extruder_idx = 1; extruder_idx <= extruders_count; ++extruder_idx)
        extruders_out.emplace_back("Extruder " + std::to_string(extruder_idx));

    return extruders_out;
}

bool GLGizmoMmuSegmentation::on_init()
{
    // FIXME Lukas H.: Discuss and change shortcut
    m_shortcut_key = WXK_CONTROL_N;

    m_desc["reset_direction"]      = _L("Reset direction");
    m_desc["clipping_of_view"]     = _L("Clipping of view") + ": ";
    m_desc["cursor_size"]          = _L("Brush size") + ": ";
    m_desc["cursor_type"]          = _L("Brush shape") + ": ";
    m_desc["first_color_caption"]  = _L("Left mouse button") + ": ";
    m_desc["first_color"]          = _L("First color");
    m_desc["second_color_caption"] = _L("Right mouse button") + ": ";
    m_desc["second_color"]         = _L("Second color");
    m_desc["remove_caption"]       = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]               = _L("Remove painted color");
    m_desc["remove_all"]           = _L("Remove all painted colors");
    m_desc["circle"]               = _L("Circle");
    m_desc["sphere"]               = _L("Sphere");
    m_desc["seed_fill_angle"]      = _L("Seed fill angle");

    m_extruders_names  = get_extruders_names();
    m_extruders_colors = get_extruders_colors();

    return true;
}

void GLGizmoMmuSegmentation::render_painter_gizmo() const
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);

    m_c->object_clipper()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

bool GLGizmoMmuSegmentation::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp
        || action == SLAGizmoEventType::MouseWheelDown) {
        if (control_down) {
            double pos = m_c->object_clipper()->get_position();
            pos = action == SLAGizmoEventType::MouseWheelDown
                  ? std::max(0., pos - 0.01)
                  : std::min(1., pos + 0.01);
            m_c->object_clipper()->set_position(pos, true);
            return true;
        }
        else if (alt_down) {
            m_cursor_radius = action == SLAGizmoEventType::MouseWheelDown
                              ? std::max(m_cursor_radius - CursorRadiusStep, CursorRadiusMin)
                              : std::min(m_cursor_radius + CursorRadiusStep, CursorRadiusMax);
            m_parent.set_as_dirty();
            return true;
        }
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position(-1., false);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown
        || action == SLAGizmoEventType::RightDown
        || (action == SLAGizmoEventType::Dragging && m_button_down != Button::None)) {

        if (m_triangle_selectors.empty())
            return false;

        EnforcerBlockerType new_state = EnforcerBlockerType::NONE;
        if (! shift_down) {
            if (action == SLAGizmoEventType::Dragging)
                new_state = m_button_down == Button::Left
                            ? EnforcerBlockerType(m_first_selected_extruder_idx)
                            : EnforcerBlockerType(m_second_selected_extruder_idx);
            else
                new_state = action == SLAGizmoEventType::LeftDown
                            ? EnforcerBlockerType(m_first_selected_extruder_idx)
                            : EnforcerBlockerType(m_second_selected_extruder_idx);
        }

        const Camera& camera = wxGetApp().plater()->get_camera();
        const Selection& selection = m_parent.get_selection();
        const ModelObject* mo = m_c->selection_info()->model_object();
        const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
        const Transform3d& instance_trafo = mi->get_transformation().get_matrix();

        // List of mouse positions that will be used as seeds for painting.
        std::vector<Vec2d> mouse_positions{mouse_position};

        // In case current mouse position is far from the last one,
        // add several positions from between into the list, so there
        // are no gaps in the painted region.
        {
            if (m_last_mouse_click == Vec2d::Zero())
                m_last_mouse_click = mouse_position;
            // resolution describes minimal distance limit using circle radius
            // as a unit (e.g., 2 would mean the patches will be touching).
            double resolution = 0.7;
            double diameter_px =  resolution  * m_cursor_radius * camera.get_zoom();
            int patches_in_between = int(((mouse_position - m_last_mouse_click).norm() - diameter_px) / diameter_px);
            if (patches_in_between > 0) {
                Vec2d diff = (mouse_position - m_last_mouse_click)/(patches_in_between+1);
                for (int i=1; i<=patches_in_between; ++i)
                    mouse_positions.emplace_back(m_last_mouse_click + i*diff);
            }
        }
        m_last_mouse_click = Vec2d::Zero(); // only actual hits should be saved

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part())
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
        }

        // Now "click" into all the prepared points and spill paint around them.
        for (const Vec2d& mp : mouse_positions) {
            update_raycast_cache(mp, camera, trafo_matrices);

            bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);

            // The mouse button click detection is enabled when there is a valid hit.
            // Missing the object entirely
            // shall not capture the mouse.
            if (m_rr.mesh_id != -1) {
                if (m_button_down == Button::None)
                    m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);
            }

            if (m_rr.mesh_id == -1) {
                // In case we have no valid hit, we can return. The event will be stopped when
                // dragging while painting (to prevent scene rotations and moving the object)
                return dragging_while_painting;
            }

            const Transform3d& trafo_matrix = trafo_matrices[m_rr.mesh_id];

            // Calculate direction from camera to the hit (in mesh coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();

            assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
            if (m_seed_fill_enabled)
                m_triangle_selectors[m_rr.mesh_id]->seed_fill_apply_on_triangles(new_state);
            else
                m_triangle_selectors[m_rr.mesh_id]->select_patch(m_rr.hit, m_rr.facet, camera_pos, m_cursor_radius, m_cursor_type,
                                                                 new_state, trafo_matrix, m_triangle_splitting_enabled);
            m_last_mouse_click = mouse_position;
        }

        return true;
    }

    if (action == SLAGizmoEventType::Moving && m_seed_fill_enabled) {
        if (m_triangle_selectors.empty())
            return false;

        const Camera &       camera         = wxGetApp().plater()->get_camera();
        const Selection &    selection      = m_parent.get_selection();
        const ModelObject *  mo             = m_c->selection_info()->model_object();
        const ModelInstance *mi             = mo->instances[selection.get_instance_idx()];
        const Transform3d &  instance_trafo = mi->get_transformation().get_matrix();

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part())
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());

        // Now "click" into all the prepared points and spill paint around them.
        update_raycast_cache(mouse_position, camera, trafo_matrices);

        if (m_rr.mesh_id == -1) {
            // Clean selected by seed fill for all triangles
            for (auto &triangle_selector : m_triangle_selectors)
                triangle_selector->seed_fill_unselect_all_triangles();

            // In case we have no valid hit, we can return.
            return false;
        }

        assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
        m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, m_rr.facet, m_seed_fill_angle);
        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
        && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name;
        if (get_painter_type() == PainterGizmoType::FDM_SUPPORTS) {
            if (shift_down)
                action_name = _L("Remove selection");
            else {
                if (m_button_down == Button::Left)
                    action_name = _L("Add supports");
                else
                    action_name = _L("Block supports");
            }
        }
        if (get_painter_type() == PainterGizmoType::SEAM) {
            if (shift_down)
                action_name = _L("Remove selection");
            else {
                if (m_button_down == Button::Left)
                    action_name = _L("Enforce seam");
                else
                    action_name = _L("Block seam");
            }
        }

        activate_internal_undo_redo_stack(true);
        Plater::TakeSnapshot(wxGetApp().plater(), action_name);
        update_model_object();

        m_button_down = Button::None;
        m_last_mouse_click = Vec2d::Zero();
        return true;
    }

    return false;
}

static void render_extruders_combo(const std::string                         &label,
                                   const std::vector<std::string>            &extruders,
                                   const std::vector<std::array<uint8_t, 3>> &extruders_colors,
                                   size_t                                    &selection_idx)
{
    assert(!extruders_colors.empty());
    assert(extruders_colors.size() == extruders_colors.size());

    size_t selection_out = selection_idx;

    // It is necessary to use BeginGroup(). Otherwise, when using SameLine() is called, then other items will be drawn inside the combobox.
    ImGui::BeginGroup();
    ImVec2 combo_pos = ImGui::GetCursorScreenPos();
    if (ImGui::BeginCombo(label.c_str(), "")) {
        for (size_t extruder_idx = 0; extruder_idx < extruders.size(); ++extruder_idx) {
            ImGui::PushID(extruder_idx);
            ImVec2 start_position = ImGui::GetCursorScreenPos();

            if (ImGui::Selectable("", extruder_idx == selection_idx))
                selection_out = extruder_idx;

            ImGui::SameLine();
            ImGuiStyle &style  = ImGui::GetStyle();
            float       height = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(start_position, ImVec2(start_position.x + height + height / 2, start_position.y + height),
                                                      IM_COL32(extruders_colors[extruder_idx][0], extruders_colors[extruder_idx][1], extruders_colors[extruder_idx][2], 255));
            ImGui::GetWindowDrawList()->AddRect(start_position, ImVec2(start_position.x + height + height / 2, start_position.y + height), IM_COL32_BLACK);

            ImGui::SetCursorScreenPos(ImVec2(start_position.x + height + height / 2 + style.FramePadding.x, start_position.y));
            ImGui::Text("%s", extruders[extruder_idx].c_str());
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    ImVec2      backup_pos = ImGui::GetCursorScreenPos();
    ImGuiStyle &style      = ImGui::GetStyle();

    ImGui::SetCursorScreenPos(ImVec2(combo_pos.x + style.FramePadding.x, combo_pos.y + style.FramePadding.y));
    ImVec2 p      = ImGui::GetCursorScreenPos();
    float  height = ImGui::GetTextLineHeight();

    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + height + height / 2, p.y + height),
                                              IM_COL32(extruders_colors[selection_idx][0], extruders_colors[selection_idx][1],
                                                       extruders_colors[selection_idx][2], 255));
    ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + height + height / 2, p.y + height), IM_COL32_BLACK);

    ImGui::SetCursorScreenPos(ImVec2(p.x + height + height / 2 + style.FramePadding.x, p.y));
    ImGui::Text("%s", extruders[selection_out].c_str());
    ImGui::SetCursorScreenPos(backup_pos);
    ImGui::EndGroup();

    selection_idx = selection_out;
}

void GLGizmoMmuSegmentation::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(23.0f);
                            y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x,
                                                m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float cursor_slider_left       = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
    const float autoset_slider_left      = m_imgui->calc_text_size(m_desc.at("seed_fill_angle")).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_left   = m_imgui->calc_text_size(m_desc.at("cursor_type")).x + m_imgui->scaled(1.f);
    const float cursor_type_radio_width1 = m_imgui->calc_text_size(m_desc["circle"]).x + m_imgui->scaled(2.5f);
    const float cursor_type_radio_width2 = m_imgui->calc_text_size(m_desc["sphere"]).x + m_imgui->scaled(2.5f);
    const float button_width             = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
    const float buttons_width            = m_imgui->scaled(0.5f);
    const float minimal_slider_width     = m_imgui->scaled(4.f);
    const float color_button_width       = m_imgui->calc_text_size("").x + m_imgui->scaled(1.75f);
    const float combo_label_width        = std::max(m_imgui->calc_text_size(m_desc.at("first_color")).x,
                                                    m_imgui->calc_text_size(m_desc.at("second_color")).x) + m_imgui->scaled(1.f);

    float caption_max    = 0.f;
    float total_text_max = 0.;
    for (const std::string &t : {"first_color", "second_color", "remove"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc.at(t + "_caption")).x);
        total_text_max = std::max(total_text_max, caption_max + m_imgui->calc_text_size(m_desc.at(t)).x);
    }
    caption_max += m_imgui->scaled(1.f);
    total_text_max += m_imgui->scaled(1.f);

    float window_width = minimal_slider_width + std::max(autoset_slider_left, std::max(cursor_slider_left, clipping_slider_left));
    window_width       = std::max(window_width, total_text_max);
    window_width       = std::max(window_width, button_width);
    window_width       = std::max(window_width, cursor_type_radio_left + cursor_type_radio_width1 + cursor_type_radio_width2);
    window_width       = std::max(window_width, 2.f * buttons_width + m_imgui->scaled(1.f));

    auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
        m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, caption);
        ImGui::SameLine(caption_max);
        m_imgui->text(text);
    };

    for (const std::string &t : {"first_color", "second_color", "remove"})
        draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

    m_imgui->text("");
    ImGui::Separator();

    const std::array<uint8_t, 3> &select_first_color  = m_extruders_colors[m_first_selected_extruder_idx];
    const std::array<uint8_t, 3> &select_second_color = m_extruders_colors[m_second_selected_extruder_idx];

    m_imgui->text(m_desc.at("first_color"));
    ImGui::SameLine(combo_label_width);
    ImGui::PushItemWidth(window_width - combo_label_width - color_button_width);
    render_extruders_combo("##first_color_combo", m_extruders_names, get_extruders_colors(), m_first_selected_extruder_idx);
    ImGui::SameLine();

    ImVec4 first_color = ImVec4(float(select_first_color[0]) / 255.0f, float(select_first_color[1]) / 255.0f, float(select_first_color[2]) / 255.0f, 1.0f);
    ImVec4 second_color = ImVec4(float(select_second_color[0]) / 255.0f, float(select_second_color[1]) / 255.0f, float(select_second_color[2]) / 255.0f, 1.0f);
    if(ImGui::ColorEdit4("First color##color_picker", (float*)&first_color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
        m_extruders_colors[m_first_selected_extruder_idx] = {uint8_t(first_color.x * 255.0f), uint8_t(first_color.y * 255.0f), uint8_t(first_color.z * 255.0f)};

    m_imgui->text(m_desc.at("second_color"));
    ImGui::SameLine(combo_label_width);
    ImGui::PushItemWidth(window_width - combo_label_width - color_button_width);
    render_extruders_combo("##second_color_combo", m_extruders_names, get_extruders_colors(), m_second_selected_extruder_idx);
    ImGui::SameLine();
    if(ImGui::ColorEdit4("Second color##color_picker", (float*)&second_color, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
        m_extruders_colors[m_second_selected_extruder_idx] = {uint8_t(second_color.x * 255.0f), uint8_t(second_color.y * 255.0f), uint8_t(second_color.z * 255.0f)};

    ImGui::Separator();

    if (m_imgui->checkbox(_L("Seed fill"), m_seed_fill_enabled))
        if (!m_seed_fill_enabled)
            for (auto &triangle_selector : m_triangle_selectors)
                triangle_selector->seed_fill_unselect_all_triangles();

    m_imgui->text(m_desc["seed_fill_angle"] + ":");
    ImGui::AlignTextToFramePadding();
    std::string format_str = std::string("%.f") + I18N::translate_utf8("°", "Degree sign to use in the respective slider in FDM supports gizmo,"
                                                                            "placed after the number with no whitespace in between.");
    ImGui::SameLine(autoset_slider_left);
    ImGui::PushItemWidth(window_width - autoset_slider_left);
    m_imgui->disabled_begin(!m_seed_fill_enabled);
    m_imgui->slider_float("##seed_fill_angle", &m_seed_fill_angle, 0.f, 90.f, format_str.data());
    m_imgui->disabled_end();

    ImGui::Separator();

    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot(wxGetApp().plater(), wxString(_L("Reset selection")));
        ModelObject *mo  = m_c->selection_info()->model_object();
        int          idx = -1;
        for (ModelVolume *mv : mo->volumes) {
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
            }
        }

        update_model_object();
        m_parent.set_as_dirty();
    }

    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_size"));
    ImGui::SameLine(cursor_slider_left);
    ImGui::PushItemWidth(window_width - cursor_slider_left);
    ImGui::SliderFloat(" ", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Alt + Mouse wheel").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_type"));
    ImGui::SameLine(cursor_type_radio_left + m_imgui->scaled(0.f));
    ImGui::PushItemWidth(cursor_type_radio_width1);

    bool sphere_sel = m_cursor_type == TriangleSelector::CursorType::SPHERE;
    if (m_imgui->radio_button(m_desc["sphere"], sphere_sel))
        sphere_sel = true;

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Paints all facets inside, regardless of their orientation.").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    ImGui::SameLine(cursor_type_radio_left + cursor_type_radio_width2 + m_imgui->scaled(0.f));
    ImGui::PushItemWidth(cursor_type_radio_width2);

    if (m_imgui->radio_button(m_desc["circle"], !sphere_sel))
        sphere_sel = false;

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Ignores facets facing away from the camera.").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    m_cursor_type = sphere_sel ? TriangleSelector::CursorType::SPHERE : TriangleSelector::CursorType::CIRCLE;

    m_imgui->checkbox(_L("Split triangles"), m_triangle_splitting_enabled);

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    } else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position(-1., false); });
        }
    }

    ImGui::SameLine(clipping_slider_left);
    ImGui::PushItemWidth(window_width - clipping_slider_left);
    float clp_dist = m_c->object_clipper()->get_position();
    if (ImGui::SliderFloat("  ", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position(clp_dist, true);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted(_L("Ctrl + Mouse wheel").ToUTF8().data());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    m_imgui->end();
}

void GLGizmoMmuSegmentation::update_model_object() const
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->mmu_segmentation_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLGizmoMmuSegmentation::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorMmuGui>(*mesh, wxGetApp().extruders_cnt(), m_extruders_colors));
        m_triangle_selectors.back()->deserialize(mv->mmu_segmentation_facets.get_data());
    }
}

PainterGizmoType GLGizmoMmuSegmentation::get_painter_type() const
{
    return PainterGizmoType::MMU_SEGMENTATION;
}

void TriangleSelectorMmuGui::render(ImGuiWrapper *imgui)
{
    std::vector<int> color_cnt(m_iva_colors.size());
    int              seed_fill_cnt = 0;
    for (auto &iva_color : m_iva_colors)
        iva_color.release_geometry();
    m_iva_seed_fill.release_geometry();

    for (size_t color_idx = 0; color_idx < m_iva_colors.size(); ++color_idx) {
        for (const Triangle &tr : m_triangles) {
            if (!tr.valid || tr.is_split() || /*tr.get_state() == EnforcerBlockerType::NONE ||*/ tr.is_selected_by_seed_fill() ||
                tr.get_state() != EnforcerBlockerType(color_idx))
                continue;

            for (int i = 0; i < 3; ++i)
                m_iva_colors[color_idx].push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                                                      double(m_vertices[tr.verts_idxs[i]].v[1]),
                                                      double(m_vertices[tr.verts_idxs[i]].v[2]),
                                                      double(tr.normal[0]),
                                                      double(tr.normal[1]),
                                                      double(tr.normal[2]));
            m_iva_colors[color_idx].push_triangle(color_cnt[color_idx], color_cnt[color_idx] + 1, color_cnt[color_idx] + 2);
            color_cnt[color_idx] += 3;
        }
    }

    for (const Triangle &tr : m_triangles) {
        if (!tr.valid || tr.is_split() || !tr.is_selected_by_seed_fill()) continue;

        for (int i = 0; i < 3; ++i)
            m_iva_seed_fill.push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                                          double(m_vertices[tr.verts_idxs[i]].v[1]),
                                          double(m_vertices[tr.verts_idxs[i]].v[2]),
                                          double(tr.normal[0]),
                                          double(tr.normal[1]),
                                          double(tr.normal[2]));
        m_iva_seed_fill.push_triangle(seed_fill_cnt, seed_fill_cnt + 1, seed_fill_cnt + 2);
        seed_fill_cnt += 3;
    }

    for (auto &iva_color : m_iva_colors)
        iva_color.finalize_geometry(true);
    m_iva_seed_fill.finalize_geometry(true);

    std::vector<bool> render_colors(m_iva_colors.size());
    for (size_t color_idx = 0; color_idx < m_iva_colors.size(); ++color_idx)
        render_colors[color_idx] = m_iva_colors[color_idx].has_VBOs();
    bool render_seed_fill = m_iva_seed_fill.has_VBOs();

    auto *shader = wxGetApp().get_shader("gouraud");
    if (!shader) return;

    shader->start_using();
    ScopeGuard guard([shader]() {
        if (shader)
            shader->stop_using();
    });
    shader->set_uniform("slope.actived", false);

    for (size_t color_idx = 0; color_idx < m_iva_colors.size(); ++color_idx) {
        if (render_colors[color_idx]) {
            std::array<float, 4> color = {m_colors[color_idx][0] / 255.0f, m_colors[color_idx][1] / 255.0f, m_colors[color_idx][2] / 255.0f, 1.f};
            shader->set_uniform("uniform_color", color);
            m_iva_colors[color_idx].render();
        }
    }

    if (render_seed_fill) {
        std::array<float, 4> color = {0.f, 1.00f, 0.44f, 1.f};
        shader->set_uniform("uniform_color", color);
        m_iva_seed_fill.render();
    }
}

} // namespace Slic3r
