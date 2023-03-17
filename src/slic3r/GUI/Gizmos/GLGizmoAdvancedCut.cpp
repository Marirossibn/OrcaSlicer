// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

#include <algorithm>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"

#include <imgui/imgui_internal.h>

namespace Slic3r {
namespace GUI {

const double       units_in_to_mm = 25.4;
const double       units_mm_to_in = 1 / units_in_to_mm;

const int c_connectors_group_id = 4;
const float UndefFloat = -999.f;

// connector colors

using ColorRGBA = std::array<float, 4>;

static const ColorRGBA BLACK() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
static const ColorRGBA BLUE() { return {0.0f, 0.0f, 1.0f, 1.0f}; }
static const ColorRGBA BLUEISH() { return {0.5f, 0.5f, 1.0f, 1.0f}; }
static const ColorRGBA CYAN() { return {0.0f, 1.0f, 1.0f, 1.0f}; }
static const ColorRGBA DARK_GRAY() { return {0.25f, 0.25f, 0.25f, 1.0f}; }
static const ColorRGBA DARK_YELLOW() { return {0.5f, 0.5f, 0.0f, 1.0f}; }
static const ColorRGBA GRAY() { return {0.5f, 0.5f, 0.5f, 1.0f}; }
static const ColorRGBA GREEN() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
static const ColorRGBA GREENISH() { return {0.5f, 1.0f, 0.5f, 1.0f}; }
static const ColorRGBA LIGHT_GRAY() { return {0.75f, 0.75f, 0.75f, 1.0f}; }
static const ColorRGBA MAGENTA() { return {1.0f, 0.0f, 1.0f, 1.0f}; }
static const ColorRGBA ORANGE() { return {0.923f, 0.504f, 0.264f, 1.0f}; }
static const ColorRGBA RED() { return {1.0f, 0.0f, 0.0f, 1.0f}; }
static const ColorRGBA REDISH() { return {1.0f, 0.5f, 0.5f, 1.0f}; }
static const ColorRGBA YELLOW() { return {1.0f, 1.0f, 0.0f, 1.0f}; }
static const ColorRGBA WHITE() { return {1.0f, 1.0f, 1.0f, 1.0f}; }

static const ColorRGBA PLAG_COLOR           = YELLOW();
static const ColorRGBA DOWEL_COLOR          = DARK_YELLOW();
static const ColorRGBA HOVERED_PLAG_COLOR   = CYAN();
static const ColorRGBA HOVERED_DOWEL_COLOR  = {0.0f, 0.5f, 0.5f, 1.0f};
static const ColorRGBA SELECTED_PLAG_COLOR  = GRAY();
static const ColorRGBA SELECTED_DOWEL_COLOR = GRAY(); // DARK_GRAY();
static const ColorRGBA CONNECTOR_DEF_COLOR  = {1.0f, 1.0f, 1.0f, 0.5f};
static const ColorRGBA CONNECTOR_ERR_COLOR  = {1.0f, 0.3f, 0.3f, 0.5f};
static const ColorRGBA HOVERED_ERR_COLOR    = {1.0f, 0.3f, 0.3f, 1.0f};

static Vec3d rotate_vec3d_around_vec3d_with_rotate_matrix(
    const Vec3d& rotate_point,
    const Vec3d& origin_point,
    const Transform3d& rotate_matrix)
{
    Transform3d translate_to_point = Transform3d::Identity();
    translate_to_point.translate(origin_point);
    Transform3d translate_to_zero = Transform3d::Identity();
    translate_to_zero.translate(-origin_point);
    return (translate_to_point * rotate_matrix * translate_to_zero) * rotate_point;
}

static inline void rotate_point_2d(double& x, double& y, const double c, const double s)
{
    double xold = x;
    double yold = y;
    x = c * xold - s * yold;
    y = s * xold + c * yold;
}

static void rotate_x_3d(std::array<Vec3d, 4>& verts, float radian_angle)
{
    double c = cos(radian_angle);
    double s = sin(radian_angle);
    for (uint32_t i = 0; i < verts.size(); ++i)
        rotate_point_2d(verts[i](1), verts[i](2), c, s);
}

static void rotate_y_3d(std::array<Vec3d, 4>& verts, float radian_angle)
{
    double c = cos(radian_angle);
    double s = sin(radian_angle);
    for (uint32_t i = 0; i < verts.size(); ++i)
        rotate_point_2d(verts[i](2), verts[i](0), c, s);
}

static void rotate_z_3d(std::array<Vec3d, 4>& verts, float radian_angle)
{
    double c = cos(radian_angle);
    double s = sin(radian_angle);
    for (uint32_t i = 0; i < verts.size(); ++i)
        rotate_point_2d(verts[i](0), verts[i](1), c, s);
}

const double GLGizmoAdvancedCut::Offset = 10.0;
const double GLGizmoAdvancedCut::Margin = 20.0;
const std::array<float, 4> GLGizmoAdvancedCut::GrabberColor      = { 1.0, 1.0, 0.0, 1.0 };
const std::array<float, 4> GLGizmoAdvancedCut::GrabberHoverColor = { 0.7, 0.7, 0.0, 1.0};

GLGizmoAdvancedCut::GLGizmoAdvancedCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoRotate3D(parent, icon_filename, sprite_id, nullptr)
    , m_movement(0.0)
    , m_buffered_movement(0.0)
    , m_last_active_id(0)
    , m_keep_upper(true)
    , m_keep_lower(true)
    , m_do_segment(false)
    , m_segment_smoothing_alpha(0.5)
    , m_segment_number(5)
    , m_connector_type(CutConnectorType::Plug)
    , m_connector_style(size_t(CutConnectorStyle::Prizm))
    , m_connector_shape_id(size_t(CutConnectorShape::Circle))
{
    for (int i = 0; i < 4; i++)
        m_cut_plane_points[i] = { 0., 0., 0. };

    set_group_id(m_gizmos.size());
    m_rotation.setZero();
    //m_current_base_rotation.setZero();
    m_rotate_cmds.clear();
    m_buffered_rotation.setZero();
}

bool GLGizmoAdvancedCut::gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

    if (shift_down && !m_connectors_editing &&
        (action == SLAGizmoEventType::LeftDown || action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::Dragging)) {
        process_cut_line(action, mouse_position);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown) {
        if (!m_connectors_editing)
            return false;

        if (m_hover_id != -1) {
            start_dragging();
            return true;
        }

        if (shift_down || alt_down) {
            // left down with shift - show the selection rectangle:
            //if (m_hover_id == -1)
            //    m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::EState::Select : GLSelectionRectangle::EState::Deselect);
        } else {
            // If there is no selection and no hovering, add new point
            if (m_hover_id == -1 && !shift_down && !alt_down)
                add_connector(connectors, mouse_position);
                    //m_ldown_mouse_position = mouse_position;
        }
        return true;
    }
    else if (action == SLAGizmoEventType::LeftUp) {
        if (m_hover_id == -1 && !shift_down && !alt_down)
            unselect_all_connectors();

        is_selection_changed(alt_down, shift_down);
        return true;
    }
    else if (action == SLAGizmoEventType::RightDown) {
        if (m_hover_id < c_connectors_group_id)
            return false;

        unselect_all_connectors();
        select_connector(m_hover_id - c_connectors_group_id, true);
        return delete_selected_connectors();
    }
    else if (action == SLAGizmoEventType::RightUp) {
        // catch right click event
        return true;
    }

    return false;
}

bool GLGizmoAdvancedCut::on_key(wxKeyEvent &evt)
{
    bool ctrl_down = evt.GetModifiers() & wxMOD_CONTROL;

    if (evt.GetKeyCode() == WXK_DELETE) {
        return delete_selected_connectors();
    }
    else if (ctrl_down
        && (evt.GetKeyCode() == 'A' || evt.GetKeyCode() == 'a'))
    {
        select_all_connectors();
        return true;
    }
    return false;
}

std::string GLGizmoAdvancedCut::get_tooltip() const
{
    return "";
}

BoundingBoxf3 GLGizmoAdvancedCut::bounding_box() const
{
    BoundingBoxf3                 ret;
    const Selection &             selection = m_parent.get_selection();
    const Selection::IndicesList &idxs      = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume *volume = selection.get_volume(i);
        // respect just to the solid parts for FFF and ignore pad and supports for SLA
        if (!volume->is_modifier && !volume->is_sla_pad() && !volume->is_sla_support()) ret.merge(volume->transformed_convex_hull_bounding_box());
    }
    return ret;
}

bool GLGizmoAdvancedCut::is_looking_forward() const
{
    const Camera &camera = wxGetApp().plater()->get_camera();
    const double  dot    = camera.get_dir_forward().dot(m_cut_plane_normal);
    return dot < 0.05;
}

// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoAdvancedCut::unproject_on_cut_plane(const Vec2d &mouse_pos, Vec3d &pos, Vec3d &pos_world)
{
    const float sla_shift = m_c->selection_info()->get_sla_shift();

    const ModelObject *  mo     = m_c->selection_info()->model_object();
    const ModelInstance *mi     = mo->instances[m_c->selection_info()->get_active_instance()];
    const Camera &       camera = wxGetApp().plater()->get_camera();

    // Calculate intersection with the clipping plane.
    const ClippingPlane *cp = m_c->object_clipper()->get_clipping_plane();
    Vec3d                point;
    Vec3d                direction;
    Vec3d                hit;
    MeshRaycaster::line_from_mouse_pos_static(mouse_pos, Transform3d::Identity(), camera, point, direction);
    Vec3d  normal = -cp->get_normal().cast<double>();
    double den    = normal.dot(direction);
    if (den != 0.) {
        double t = (-cp->get_offset() - normal.dot(point)) / den;
        hit      = (point + t * direction);
    } else
        return false;

    if (!m_c->object_clipper()->is_projection_inside_cut(hit))
        return false;

    // recalculate hit to object's local position
    Vec3d hit_d = hit;
    hit_d -= mi->get_offset();
    hit_d[Z] -= sla_shift;

    // Return both the point and the facet normal.
    pos       = hit_d;
    pos_world = hit;

    return true;
}

void GLGizmoAdvancedCut::update_plane_points()
{
    Vec3d plane_center = get_plane_center();

    std::array<Vec3d, 4> plane_points_rot;
    for (int i = 0; i < plane_points_rot.size(); i++) {
        plane_points_rot[i] = m_cut_plane_points[i] - plane_center;
    }

    if (m_rotation(0) > EPSILON) {
        rotate_x_3d(plane_points_rot, m_rotation(0));
        m_rotate_cmds.emplace(m_rotate_cmds.begin(), m_rotation(0), X);
    }
    if (m_rotation(1) > EPSILON) {
        rotate_y_3d(plane_points_rot, m_rotation(1));
        m_rotate_cmds.emplace(m_rotate_cmds.begin(), m_rotation(1), Y);
    }
    if (m_rotation(2) > EPSILON) {
        rotate_z_3d(plane_points_rot, m_rotation(2));
        m_rotate_cmds.emplace(m_rotate_cmds.begin(), m_rotation(2), Z);
    }

    Vec3d plane_normal = calc_plane_normal(plane_points_rot);
    if (m_movement == 0 && m_height_delta != 0)
        m_movement = plane_normal(2) * m_height_delta;// plane_normal.dot(Vec3d(0, 0, m_height_delta))
    for (int i = 0; i < plane_points_rot.size(); i++) {
        m_cut_plane_points[i] = plane_points_rot[i] + plane_center + plane_normal * m_movement;
    }

    //m_current_base_rotation += m_rotation;
    m_rotation.setZero();
    m_movement = 0.0;
    m_height_delta = 0;
}

std::array<Vec3d, 4> GLGizmoAdvancedCut::get_plane_points() const
{
    return m_cut_plane_points;
}

std::array<Vec3d, 4> GLGizmoAdvancedCut::get_plane_points_world_coord() const
{
    std::array<Vec3d, 4> plane_world_coord = m_cut_plane_points;

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    Vec3d object_offset = box.center();

    for (Vec3d& point : plane_world_coord) {
        point += object_offset;
    }

    return plane_world_coord;
}

void GLGizmoAdvancedCut::reset_cut_plane()
{
    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    const float max_x = box.size()(0) / 2.0 + Margin;
    const float min_x = -max_x;
    const float max_y = box.size()(1) / 2.0 + Margin;
    const float min_y = -max_y;

    m_cut_plane_points[0] = { min_x, min_y, 0 };
    m_cut_plane_points[1] = { max_x, min_y, 0 };
    m_cut_plane_points[2] = { max_x, max_y, 0 };
    m_cut_plane_points[3] = { min_x, max_y, 0 };
    m_movement = 0.0;
    m_height = box.size()[2] / 2.0;
    m_height_delta = 0;
    m_rotation.setZero();
    //m_current_base_rotation.setZero();
    m_rotate_cmds.clear();

    m_buffered_movement = 0.0;
    m_buffered_height = m_height;
    m_buffered_rotation.setZero();
}

void GLGizmoAdvancedCut::reset_all()
{
    reset_cut_plane();

    m_keep_upper = true;
    m_keep_lower = true;
    m_place_on_cut_upper = true;
    m_place_on_cut_lower = false;
    m_rotate_upper = false;
    m_rotate_lower = false;
}

bool GLGizmoAdvancedCut::on_init()
{
    if (!GLGizmoRotate3D::on_init())
        return false;

    m_shortcut_key = WXK_CONTROL_C;

    // initiate info shortcuts
    const wxString ctrl  = GUI::shortkey_ctrl_prefix();
    const wxString alt   = GUI::shortkey_alt_prefix();
    const wxString shift = "Shift+";

    m_shortcuts.push_back(std::make_pair(_L("Left click"), _L("Add connector")));
    m_shortcuts.push_back(std::make_pair(_L("Right click"), _L("Remove connector")));
    m_shortcuts.push_back(std::make_pair(_L("Drag"), _L("Move connector")));
    m_shortcuts.push_back(std::make_pair(shift + _L("Left click"), _L("Add connector to selection")));
    m_shortcuts.push_back(std::make_pair(alt + _L("Left click"), _L("Remove connector from selection")));
    m_shortcuts.push_back(std::make_pair(ctrl + "A", _L("Select all connectors")));

    init_connector_shapes();
    return true;
}

std::string GLGizmoAdvancedCut::on_get_name() const
{
    return (_(L("Cut"))).ToUTF8().data();
}

void GLGizmoAdvancedCut::on_load(cereal::BinaryInputArchive &ar)
{
    ar(m_keep_upper, m_keep_lower, m_rotate_lower, m_rotate_upper, m_connectors_editing,
        m_cut_plane_points[0], m_cut_plane_points[1], m_cut_plane_points[2], m_cut_plane_points[3]);

    m_parent.request_extra_frame();
}

void GLGizmoAdvancedCut::on_save(cereal::BinaryOutputArchive &ar) const
{
    ar(m_keep_upper, m_keep_lower, m_rotate_lower, m_rotate_upper, m_connectors_editing,
        m_cut_plane_points[0], m_cut_plane_points[1], m_cut_plane_points[2], m_cut_plane_points[3]);
}

void GLGizmoAdvancedCut::on_set_state()
{
    GLGizmoRotate3D::on_set_state();

    // Reset m_cut_z on gizmo activation
    if (get_state() == On) {
        m_connectors_editing = false;
        reset_cut_plane();
    }
    else if (get_state() == Off) {
        clear_selection();
        m_c->object_clipper()->release();
    }
}

bool GLGizmoAdvancedCut::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

CommonGizmosDataID GLGizmoAdvancedCut::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider)
        | int(CommonGizmosDataID::Raycaster)
        | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoAdvancedCut::on_start_dragging()
{
    for (auto gizmo : m_gizmos) {
        if (m_hover_id == gizmo.get_group_id()) {
            gizmo.start_dragging();
            return;
        }
    }

    if (m_hover_id != get_group_id())
        return;

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    m_start_movement = m_movement;
    m_start_height = m_height;
    m_drag_pos = m_move_grabber.center;

    if (m_hover_id >= c_connectors_group_id)
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Move connector");
}

void GLGizmoAdvancedCut::on_stop_dragging()
{
    if (m_hover_id == X || m_hover_id == Y || m_hover_id == Z) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Rotate cut plane");
    } else if (m_hover_id == c_connectors_group_id - 1) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Move cut plane");
    }
}

void GLGizmoAdvancedCut::on_update(const UpdateData& data)
{
    GLGizmoRotate3D::on_update(data);

    Vec3d rotation;
    for (int i = 0; i < 3; i++)
    {
        rotation(i) = m_gizmos[i].get_angle();
        if (rotation(i) < 0)
            rotation(i) = 2*PI + rotation(i);
    }

    m_rotation = rotation;
    //m_move_grabber.angles = m_current_base_rotation + m_rotation;

    if (m_hover_id == get_group_id()) {
        double move = calc_projection(data.mouse_ray);
        set_movement(m_start_movement + move);
        Vec3d plane_normal = get_plane_normal();
        m_height = m_start_height + plane_normal(2) * move;
    }

    // dragging connectors
    if (m_connectors_editing && m_hover_id >= c_connectors_group_id) {
        CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;
        Vec3d          pos;
        Vec3d          pos_world;

        if (unproject_on_cut_plane(data.mouse_pos.cast<double>(), pos, pos_world)) {
            connectors[m_hover_id - c_connectors_group_id].pos = pos;
        }
    }
}

void GLGizmoAdvancedCut::on_render()
{
    update_clipper();
    if (m_connectors_editing) {
        render_connectors();
        render_clipper_cut();
    }
    else if(!m_connectors_editing) {
        check_conflict_for_all_connectors();
        render_cut_plane_and_grabbers();
    }

    render_cut_line();
}

void GLGizmoAdvancedCut::on_render_for_picking()
{
    GLGizmoRotate3D::on_render_for_picking();

    glsafe(::glDisable(GL_DEPTH_TEST));

    BoundingBoxf3 box = m_parent.get_selection().get_bounding_box();
#if ENABLE_FIXED_GRABBER
    float mean_size = (float)(GLGizmoBase::Grabber::FixedGrabberSize);
#else
    float mean_size = (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0);
#endif

    std::array<float, 4> color = picking_color_component(0);
    m_move_grabber.color[0] = color[0];
    m_move_grabber.color[1] = color[1];
    m_move_grabber.color[2] = color[2];
    m_move_grabber.color[3] = color[3];
    m_move_grabber.render_for_picking(mean_size);

    glsafe(::glEnable(GL_DEPTH_TEST));
    auto inst_id = m_c->selection_info()->get_active_instance();
    if (inst_id < 0)
        return;

    const ModelObject *mo      = m_c->selection_info()->model_object();
    const ModelInstance *mi              = mo->instances[inst_id];
    const Vec3d &        instance_offset = mi->get_offset();
    const double         sla_shift       = double(m_c->selection_info()->get_sla_shift());

    const CutConnectors &connectors = mo->cut_connectors;
    for (int i = 0; i < connectors.size(); ++i) {
        CutConnector connector = connectors[i];
        Vec3d pos = connector.pos + instance_offset + sla_shift * Vec3d::UnitZ();
        float height = connector.height;

        const Camera &camera = wxGetApp().plater()->get_camera();
        if (connector.attribs.type == CutConnectorType::Dowel && connector.attribs.style == CutConnectorStyle::Prizm) {
            pos -= height * m_cut_plane_normal;
            height *= 2;
        } else if (!is_looking_forward())
            pos -= 0.05 * m_cut_plane_normal;

        Transform3d translate_tf = Transform3d::Identity();
        translate_tf.translate(pos);

        Transform3d scale_tf = Transform3d::Identity();
        scale_tf.scale(Vec3f(connector.radius, connector.radius, height).cast<double>());

        const Transform3d view_model_matrix = camera.get_view_matrix() * translate_tf * m_rotate_matrix * scale_tf;


        std::array<float, 4> color = picking_color_component(i+1);
        render_connector_model(m_shapes[connectors[i].attribs], color, view_model_matrix);
    }
}

void GLGizmoAdvancedCut::on_render_input_window(float x, float y, float bottom_limit)
{
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(on_get_name(),
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    if (m_connectors_editing) {
        init_connectors_input_window_data();
        render_connectors_input_window(x, y, bottom_limit);
    }
    else
        render_cut_plane_input_window(x, y, bottom_limit);

    render_input_window_warning();

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoAdvancedCut::show_tooltip_information(float x, float y)
{
    float                      caption_max = 0.f;
    for (const auto& short_cut : m_shortcuts) {
        caption_max = std::max(caption_max, m_imgui->calc_text_size(short_cut.first).x);
    }

    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += m_imgui->calc_text_size(": ").x + 35.f;

    float  font_size   = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, ImGui::GetStyle().FramePadding.y});
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };

        for (const auto& short_cut : m_shortcuts)
            draw_text_with_caption(short_cut.first + ": ", short_cut.second);
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

void GLGizmoAdvancedCut::set_movement(double movement) const
{
    m_movement = movement;
}

void GLGizmoAdvancedCut::perform_cut(const Selection& selection)
{
    if (!can_perform_cut())
        return;

    const int instance_idx = selection.get_instance_idx();
    const int object_idx = selection.get_object_idx();

    wxCHECK_RET(instance_idx >= 0 && object_idx >= 0, "GLGizmoAdvancedCut: Invalid object selection");

    // m_cut_z is the distance from the bed. Subtract possible SLA elevation.
    const GLVolume* first_glvolume = selection.get_volume(*selection.get_volume_idxs().begin());

    // perform cut
    {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Cut by Plane");

        ModelObject *mo = wxGetApp().plater()->model().objects[object_idx];
        const bool has_connectors = !mo->cut_connectors.empty();

        bool create_dowels_as_separate_object = false;
        // update connectors pos as offset of its center before cut performing
        apply_connectors_in_model(mo, create_dowels_as_separate_object);

        // BBS: do segment
        if (m_do_segment) {
            wxGetApp().plater()->segment(object_idx, instance_idx, m_segment_smoothing_alpha, m_segment_number);
        } else {
            wxGetApp().plater()->cut(object_idx, instance_idx, get_plane_points_world_coord(),
                                     only_if(m_keep_upper, ModelObjectCutAttribute::KeepUpper) |
                                     only_if(m_keep_lower, ModelObjectCutAttribute::KeepLower) |
                                     only_if(m_place_on_cut_upper, ModelObjectCutAttribute::PlaceOnCutUpper) |
                                     only_if(m_place_on_cut_lower, ModelObjectCutAttribute::PlaceOnCutLower) |
                                     only_if(m_rotate_upper, ModelObjectCutAttribute::FlipUpper) |
                                     only_if(m_rotate_lower, ModelObjectCutAttribute::FlipLower) |
                                     only_if(create_dowels_as_separate_object, ModelObjectCutAttribute::CreateDowels));
        }
    }
}

bool GLGizmoAdvancedCut::can_perform_cut() const
{
    if (m_has_invalid_connector || (!m_keep_upper && !m_keep_lower) || m_connectors_editing)
        return false;

    return true;
    //const auto clipper = m_c->object_clipper();
    //return clipper && clipper->has_valid_contour();
}

void GLGizmoAdvancedCut::apply_connectors_in_model(ModelObject *mo, bool &create_dowels_as_separate_object)
{
    clear_selection();

    for (CutConnector &connector : mo->cut_connectors) {
        connector.rotation_m = m_rotate_matrix;

        if (connector.attribs.type == CutConnectorType::Dowel) {
            if (connector.attribs.style == CutConnectorStyle::Prizm) connector.height *= 2;
            create_dowels_as_separate_object = true;
        } else {
            // culculate shift of the connector center regarding to the position on the cut plane
            Vec3d shifted_center = m_cut_plane_center + Vec3d::UnitZ();
            shifted_center       = rotate_vec3d_around_vec3d_with_rotate_matrix(shifted_center, m_cut_plane_center, m_rotate_matrix);
            Vec3d norm           = (shifted_center - m_cut_plane_center).normalized();
            connector.pos += norm * 0.5 * double(connector.height);
        }
    }

    mo->apply_cut_connectors(_u8L("Connector"));
}

bool GLGizmoAdvancedCut::is_selection_changed(bool alt_down, bool shift_down)
{
    if (m_hover_id >= c_connectors_group_id) {
        if (alt_down)
            select_connector(m_hover_id - c_connectors_group_id, false);
        else {
            if (!shift_down) unselect_all_connectors();
            select_connector(m_hover_id - c_connectors_group_id, true);
        }
        return true;
    }
    return false;
}

void GLGizmoAdvancedCut::select_connector(int idx, bool select)
{
    m_selected[idx] = select;
    if (select)
        ++m_selected_count;
    else
        --m_selected_count;
}

Vec3d GLGizmoAdvancedCut::calc_plane_normal(const std::array<Vec3d, 4>& plane_points) const
{
    Vec3d v01 = plane_points[1] - plane_points[0];
    Vec3d v12 = plane_points[2] - plane_points[1];

    Vec3d plane_normal = v01.cross(v12);
    plane_normal.normalize();
    return plane_normal;
}

Vec3d GLGizmoAdvancedCut::calc_plane_center(const std::array<Vec3d, 4>& plane_points) const
{
    Vec3d plane_center;
    plane_center.setZero();
    for (const Vec3d& point : plane_points)
        plane_center = plane_center + point;

    return plane_center / (float)m_cut_plane_points.size();
}

double GLGizmoAdvancedCut::calc_projection(const Linef3& mouse_ray) const
{
    Vec3d mouse_dir = mouse_ray.unit_vector();
    Vec3d inters = mouse_ray.a + (m_drag_pos - mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
    Vec3d inters_vec = inters - m_drag_pos;

    Vec3d plane_normal = get_plane_normal();
    return inters_vec.dot(plane_normal);
}

Vec3d GLGizmoAdvancedCut::get_plane_normal() const
{
    return calc_plane_normal(m_cut_plane_points);
}

Vec3d GLGizmoAdvancedCut::get_plane_center() const
{
    return calc_plane_center(m_cut_plane_points);
}

void GLGizmoAdvancedCut::finish_rotation()
{
    for (int i = 0; i < 3; i++) {
        m_gizmos[i].set_angle(0.);
    }

    update_plane_points();
}

void GLGizmoAdvancedCut::put_connectors_on_cut_plane(const Vec3d &cp_normal, double cp_offset)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (CutConnectors &connectors = mo->cut_connectors; !connectors.empty()) {
        const float  sla_shift       = m_c->selection_info()->get_sla_shift();
        const Vec3d &instance_offset = mo->instances[m_c->selection_info()->get_active_instance()]->get_offset();

        for (auto &connector : connectors) {
            // convert connetor pos to the world coordinates
            Vec3d pos = connector.pos + instance_offset;
            pos[Z] += sla_shift;
            // scalar distance from point to plane along the normal
            double distance = -cp_normal.dot(pos) + cp_offset;
            // move connector
            connector.pos += distance * cp_normal;
        }
    }
}

void GLGizmoAdvancedCut::update_clipper()
{
    BoundingBoxf3 box = bounding_box();
    double        radius = box.radius();
    Vec3d         plane_center = m_cut_plane_center;

    Vec3d begin, end = begin = plane_center;
    begin[Z] = box.center().z() - radius;
    end[Z] = box.center().z() + radius;

    double   phi;
    Vec3d    rotation_axis;
    Matrix3d rotation_matrix;
    Geometry::rotation_from_two_vectors(Vec3d::UnitZ(), m_cut_plane_normal, rotation_axis, phi, &rotation_matrix);

    m_rotate_matrix.setIdentity();
    m_rotate_matrix = rotation_matrix * m_rotate_matrix;

    begin = rotate_vec3d_around_vec3d_with_rotate_matrix(begin, plane_center, m_rotate_matrix);
    end   = rotate_vec3d_around_vec3d_with_rotate_matrix(end, plane_center, m_rotate_matrix);

    Vec3d normal = end - begin;

    if (!is_looking_forward()) {
        end = begin = plane_center;
        begin[Z]    = box.center().z() + radius;
        end[Z]      = box.center().z() - radius;

        begin = rotate_vec3d_around_vec3d_with_rotate_matrix(begin, plane_center, m_rotate_matrix);
        end   = rotate_vec3d_around_vec3d_with_rotate_matrix(end, plane_center, m_rotate_matrix);

        // recalculate normal for clipping plane, if camera is looking downward to cut plane
        normal = end - begin;
        if (normal == Vec3d::Zero())
            return;
    }

    // calculate normal and offset for clipping plane
    double dist = (plane_center - begin).norm();
    dist        = std::clamp(dist, 0.0001, normal.norm());
    normal.normalize();
    const double offset = normal.dot(begin) + dist;

    m_c->object_clipper()->set_range_and_pos(normal, offset, dist);

    put_connectors_on_cut_plane(normal, offset);
}

void GLGizmoAdvancedCut::render_cut_plane_and_grabbers()
{
    const Selection &    selection = m_parent.get_selection();
    const BoundingBoxf3 &box       = selection.get_bounding_box();
    // box center is the coord of object in the world coordinate
    Vec3d object_offset = box.center();
    // plane points is in object coordinate
    Vec3d plane_center = get_plane_center();

    m_cut_plane_center = object_offset + plane_center;

    // rotate plane
    std::array<Vec3d, 4> plane_points_rot;
    {
        for (int i = 0; i < plane_points_rot.size(); i++) {
            plane_points_rot[i] = m_cut_plane_points[i] - plane_center;
        }

        if (m_rotation(0) > EPSILON)
            rotate_x_3d(plane_points_rot, m_rotation(0));
        if (m_rotation(1) > EPSILON)
            rotate_y_3d(plane_points_rot, m_rotation(1));
        if (m_rotation(2) > EPSILON)
            rotate_z_3d(plane_points_rot, m_rotation(2));

        for (int i = 0; i < plane_points_rot.size(); i++) {
            plane_points_rot[i] += plane_center;
        }
    }

    // move plane
    Vec3d plane_normal_rot = calc_plane_normal(plane_points_rot);
    m_cut_plane_normal     = plane_normal_rot;
    for (int i = 0; i < plane_points_rot.size(); i++) {
        plane_points_rot[i] += plane_normal_rot * m_movement;
    }

    // transfer from object coordindate to the world coordinate
    for (Vec3d& point : plane_points_rot) {
        point += object_offset;
    }

    // draw plane
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    ::glBegin(GL_QUADS);
    ::glColor4f(0.8f, 0.8f, 0.8f, 0.5f);
    for (const Vec3d& point : plane_points_rot) {
        ::glVertex3f(point(0), point(1), point(2));
    }
    glsafe(::glEnd());

    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));

    // Draw the grabber and the connecting line
    Vec3d plane_center_rot = calc_plane_center(plane_points_rot);
    m_move_grabber.center = plane_center_rot + plane_normal_rot * Offset;
    // m_move_grabber.angles = m_current_base_rotation + m_rotation;

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glLineWidth(m_hover_id != -1 ? 2.0f : 1.5f));
    glsafe(::glColor3f(1.0, 1.0, 0.0));
    glLineStipple(1, 0x0FFF);
    glEnable(GL_LINE_STIPPLE);
    ::glBegin(GL_LINES);
    ::glVertex3dv(plane_center_rot.data());
    ::glVertex3dv(m_move_grabber.center.data());
    glsafe(::glEnd());
    glDisable(GL_LINE_STIPPLE);

    // std::copy(std::begin(GrabberColor), std::end(GrabberColor), m_move_grabber.color);
    // m_move_grabber.color = GrabberColor;
    // m_move_grabber.hover_color = GrabberHoverColor;
    // m_move_grabber.render(m_hover_id == get_group_id(), (float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0));
    bool hover = (m_hover_id == get_group_id());
    std::array<float, 4> render_color;
    if (hover) {
        render_color = GrabberHoverColor;
    }
    else
        render_color = GrabberColor;

    const GLModel &cube = m_move_grabber.get_cube();
    // BBS set to fixed size grabber
    // float fullsize = 2 * (dragging ? get_dragging_half_size(size) : get_half_size(size));
    float fullsize = 8.0f;
    if (GLGizmoBase::INV_ZOOM > 0) {
        fullsize = m_move_grabber.FixedGrabberSize * GLGizmoBase::INV_ZOOM;
    }

    const_cast<GLModel*>(&cube)->set_color(-1, render_color);

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_move_grabber.center.x(), m_move_grabber.center.y(), m_move_grabber.center.z()));
    glsafe(::glMultMatrixd(m_rotate_matrix.data()));

    glsafe(::glScaled(fullsize, fullsize, fullsize));
    cube.render();
    glsafe(::glPopMatrix());

    // Should be placed at last, because GLGizmoRotate3D clears depth buffer
    set_center(m_cut_plane_center);
    GLGizmoRotate3D::on_render();
}

void GLGizmoAdvancedCut::render_connectors()
{
    ::glEnable(GL_DEPTH_TEST);

    const ModelObject *mo      = m_c->selection_info()->model_object();
    auto               inst_id = m_c->selection_info()->get_active_instance();
    if (inst_id < 0)
        return;

    const CutConnectors &connectors = mo->cut_connectors;
    if (connectors.size() != m_selected.size()) {
        clear_selection();
        m_selected.resize(connectors.size(), false);
    }

    const ModelInstance *mi              = mo->instances[inst_id];
    const Vec3d &        instance_offset = mi->get_offset();
    const double         sla_shift       = double(m_c->selection_info()->get_sla_shift());

    m_has_invalid_connector = false;
    m_info_stats.invalidate();

    ColorRGBA render_color = CONNECTOR_DEF_COLOR;
    for (size_t i = 0; i < connectors.size(); ++i) {
        const CutConnector &connector = connectors[i];

        float height = connector.height;
        // recalculate connector position to world position
        Vec3d pos = connector.pos + instance_offset + sla_shift * Vec3d::UnitZ();

        // First decide about the color of the point.
        const bool conflict_connector = is_conflict_for_connector(i, connectors, pos);
        if (conflict_connector) {
            m_has_invalid_connector = true;
            render_color            = CONNECTOR_ERR_COLOR;
        } else // default connector color
            render_color = connector.attribs.type == CutConnectorType::Dowel ? DOWEL_COLOR : PLAG_COLOR;

        if (!m_connectors_editing)
            render_color = CONNECTOR_ERR_COLOR;
        else if (size_t(m_hover_id - 4) == i)
            render_color = conflict_connector ? HOVERED_ERR_COLOR : connector.attribs.type == CutConnectorType::Dowel ? HOVERED_DOWEL_COLOR : HOVERED_PLAG_COLOR;
        else if (m_selected[i])
            render_color = connector.attribs.type == CutConnectorType::Dowel ? SELECTED_DOWEL_COLOR : SELECTED_PLAG_COLOR;

        const Camera &camera = wxGetApp().plater()->get_camera();
        if (connector.attribs.type == CutConnectorType::Dowel && connector.attribs.style == CutConnectorStyle::Prizm) {
            pos -= height * m_cut_plane_normal;
            height *= 2;
        } else if (!is_looking_forward())
            pos -= 0.05 * m_cut_plane_normal;

        Transform3d translate_tf = Transform3d::Identity();
        translate_tf.translate(pos);

        Transform3d scale_tf = Transform3d::Identity();
        scale_tf.scale(Vec3f(connector.radius, connector.radius, height).cast<double>());

        const Transform3d view_model_matrix = camera.get_view_matrix() * translate_tf * m_rotate_matrix * scale_tf;

        //render_color = {1.f, 0.f, 0.f, 1.f};
        render_connector_model(m_shapes[connector.attribs], render_color, view_model_matrix);
    }
}

void GLGizmoAdvancedCut::render_clipper_cut()
{
    m_c->object_clipper()->render_cut();
}

void GLGizmoAdvancedCut::render_cut_line()
{
    if (!cut_line_processing() || m_cut_line_end == Vec3d::Zero())
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glColor3f(0.0, 1.0, 0.0));
    glEnable(GL_LINE_STIPPLE);
    ::glBegin(GL_LINES);
    ::glVertex3dv(m_cut_line_begin.data());
    ::glVertex3dv(m_cut_line_end.data());
    glsafe(::glEnd());
    glDisable(GL_LINE_STIPPLE);
}

void GLGizmoAdvancedCut::render_connector_model(GLModel &model, const std::array<float, 4>& color, Transform3d view_model_matrix)
{
    GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light_uniform");
    if (shader) {
        shader->start_using();

        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", wxGetApp().plater()->get_camera().get_projection_matrix());

        model.set_color(-1, color);
        model.render();

        shader->stop_using();
    }
}

void GLGizmoAdvancedCut::clear_selection()
{
    m_selected.clear();
    m_selected_count = 0;
}

void GLGizmoAdvancedCut::init_connector_shapes()
{
    for (const CutConnectorType &type : {CutConnectorType::Dowel, CutConnectorType::Plug})
        for (const CutConnectorStyle &style : {CutConnectorStyle::Frustum, CutConnectorStyle::Prizm})
            for (const CutConnectorShape &shape : {CutConnectorShape::Circle, CutConnectorShape::Hexagon, CutConnectorShape::Square, CutConnectorShape::Triangle}) {
                const CutConnectorAttributes attribs = {type, style, shape};
                const indexed_triangle_set   its     = ModelObject::get_connector_mesh(attribs);
                m_shapes[attribs].init_from(its);
            }
}

void GLGizmoAdvancedCut::set_connectors_editing(bool connectors_editing)
{
    m_connectors_editing = connectors_editing;
    // todo: zhimin need a better method
    // after change render mode, need update for scene
    on_render();
}

void GLGizmoAdvancedCut::reset_connectors()
{
    m_c->selection_info()->model_object()->cut_connectors.clear();
    clear_selection();
}

void GLGizmoAdvancedCut::update_connector_shape()
{
    CutConnectorAttributes attribs = {m_connector_type, CutConnectorStyle(m_connector_style), CutConnectorShape(m_connector_shape_id)};

    const indexed_triangle_set its = ModelObject::get_connector_mesh(attribs);
    m_connector_mesh.clear();
    m_connector_mesh = TriangleMesh(its);
}

void GLGizmoAdvancedCut::apply_selected_connectors(std::function<void(size_t idx)> apply_fn)
{
    for (size_t idx = 0; idx < m_selected.size(); idx++)
        if (m_selected[idx])
            apply_fn(idx);
}

void GLGizmoAdvancedCut::select_all_connectors()
{
    std::fill(m_selected.begin(), m_selected.end(), true);
    m_selected_count = int(m_selected.size());
}

void GLGizmoAdvancedCut::unselect_all_connectors()
{
    std::fill(m_selected.begin(), m_selected.end(), false);
    m_selected_count = 0;
    validate_connector_settings();
}

void GLGizmoAdvancedCut::validate_connector_settings()
{
    if (m_connector_depth_ratio < 0.f)
        m_connector_depth_ratio = 3.f;
    if (m_connector_depth_ratio_tolerance < 0.f)
        m_connector_depth_ratio_tolerance = 0.1f;
    if (m_connector_size < 0.f)
        m_connector_size = 2.5f;
    if (m_connector_size_tolerance < 0.f)
        m_connector_size_tolerance = 0.f;

    if (m_connector_type == CutConnectorType::Undef)
        m_connector_type = CutConnectorType::Plug;
    if (m_connector_style == size_t(CutConnectorStyle::Undef))
        m_connector_style = size_t(CutConnectorStyle::Prizm);
    if (m_connector_shape_id == size_t(CutConnectorShape::Undef))
        m_connector_shape_id = size_t(CutConnectorShape::Circle);
}

bool GLGizmoAdvancedCut::add_connector(CutConnectors &connectors, const Vec2d &mouse_position)
{
    if (!m_connectors_editing)
        return false;

    Vec3d pos;
    Vec3d pos_world;
    if (unproject_on_cut_plane(mouse_position.cast<double>(), pos, pos_world)) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Add connector");
        unselect_all_connectors();

        connectors.emplace_back(pos, m_rotate_matrix, m_connector_size * 0.5f, m_connector_depth_ratio, m_connector_size_tolerance, m_connector_depth_ratio_tolerance,
                                CutConnectorAttributes(CutConnectorType(m_connector_type), CutConnectorStyle(m_connector_style), CutConnectorShape(m_connector_shape_id)));
        m_selected.push_back(true);
        m_selected_count = 1;
        assert(m_selected.size() == connectors.size());
        m_parent.set_as_dirty();

        return true;
    }
    return false;
}

bool GLGizmoAdvancedCut::delete_selected_connectors()
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;
    if (connectors.empty())
        return false;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Delete connector");

    // remove  connectors
    for (int i = int(connectors.size()) - 1; i >= 0; i--)
        if (m_selected[i]) connectors.erase(connectors.begin() + i);
    // remove selections
    m_selected.erase(std::remove_if(m_selected.begin(), m_selected.end(),
        [](const auto &selected) {
            return selected;}),
        m_selected.end());

    m_selected_count = 0;

    assert(m_selected.size() == connectors.size());
    m_parent.set_as_dirty();
    return true;
}

bool GLGizmoAdvancedCut::is_outside_of_cut_contour(size_t idx, const CutConnectors &connectors, const Vec3d cur_pos)
{
    // check if connector pos is out of clipping plane
    if (m_c->object_clipper() && !m_c->object_clipper()->is_projection_inside_cut(cur_pos)) {
        m_info_stats.outside_cut_contour++;
        return true;
    }

    // check if connector bottom contour is out of clipping plane
    const CutConnector &    cur_connector = connectors[idx];
    const CutConnectorShape shape         = CutConnectorShape(cur_connector.attribs.shape);
    const int   sectorCount = shape == CutConnectorShape::Triangle  ? 3 :
                              shape == CutConnectorShape::Square    ? 4 :
                              shape == CutConnectorShape::Circle    ? 60: // supposably, 60 points are enough for conflict detection
                              shape == CutConnectorShape::Hexagon   ? 6 : 1 ;

    indexed_triangle_set mesh;
    auto &               vertices = mesh.vertices;
    vertices.reserve(sectorCount + 1);

    float fa  = 2 * PI / sectorCount;
    auto  vec = Eigen::Vector2f(0, cur_connector.radius);
    for (float angle = 0; angle < 2.f * PI; angle += fa) {
        Vec2f p = Eigen::Rotation2Df(angle) * vec;
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
    }

    Transform3d transform = Transform3d::Identity();
    transform.translate(cur_pos);
    its_transform(mesh, transform * m_rotate_matrix);

    for (auto vertex : vertices) {
        if (m_c->object_clipper() && !m_c->object_clipper()->is_projection_inside_cut(vertex.cast<double>())) {
            m_info_stats.outside_cut_contour++;
            return true;
        }
    }

    return false;
}

bool GLGizmoAdvancedCut::is_conflict_for_connector(size_t idx, const CutConnectors &connectors, const Vec3d cur_pos)
{
    if (is_outside_of_cut_contour(idx, connectors, cur_pos))
        return true;

    const CutConnector &cur_connector = connectors[idx];

    Transform3d translate_tf = Transform3d::Identity();
    translate_tf.translate(cur_pos);
    Transform3d scale_tf = Transform3d::Identity();
    scale_tf.scale(Vec3f(cur_connector.radius, cur_connector.radius, cur_connector.height).cast<double>());
    const Transform3d   matrix  = translate_tf * m_rotate_matrix * scale_tf;

    const BoundingBoxf3 cur_tbb = m_shapes[cur_connector.attribs].get_bounding_box().transformed(matrix);

    // check if connector's bounding box is inside the object's bounding box
    if (!bounding_box().contains(cur_tbb)) {
        m_info_stats.outside_bb++;
        return true;
    }

    // check if connectors are overlapping
    for (size_t i = 0; i < connectors.size(); ++i) {
        if (i == idx) continue;
        const CutConnector &connector = connectors[i];

        if ((connector.pos - cur_connector.pos).norm() < double(connector.radius + cur_connector.radius)) {
            m_info_stats.is_overlap = true;
            return true;
        }
    }

    return false;
}

void GLGizmoAdvancedCut::check_conflict_for_all_connectors()
{
    const ModelObject *mo      = m_c->selection_info()->model_object();
    auto               inst_id = m_c->selection_info()->get_active_instance();
    if (inst_id < 0)
        return;

    const CutConnectors &connectors      = mo->cut_connectors;
    const ModelInstance *mi              = mo->instances[inst_id];
    const Vec3d &        instance_offset = mi->get_offset();
    const double         sla_shift       = double(m_c->selection_info()->get_sla_shift());

    m_has_invalid_connector = false;
    m_info_stats.invalidate();

    for (size_t i = 0; i < connectors.size(); ++i) {
        const CutConnector &connector = connectors[i];

        Vec3d pos = connector.pos + instance_offset + sla_shift * Vec3d::UnitZ();

        // First decide about the color of the point.
        const bool conflict_connector = is_conflict_for_connector(i, connectors, pos);
        if (conflict_connector) {
            m_has_invalid_connector = true;
        }
    }
}

void GLGizmoAdvancedCut::render_cut_plane_input_window(float x, float y, float bottom_limit)
{
    // float unit_size = m_imgui->get_style_scaling() * 48.0f;
    float        space_size        = m_imgui->get_style_scaling() * 8;
    float        movement_cap      = m_imgui->calc_text_size(_L("Movement:")).x;
    float        rotate_cap        = m_imgui->calc_text_size(_L("Rotate")).x;
    float        caption_size      = std::max(movement_cap, rotate_cap) + 2 * space_size;
    bool         imperial_units    = wxGetApp().app_config->get("use_inches") == "1";
    unsigned int current_active_id = ImGui::GetActiveID();

    Vec3d rotation = {Geometry::rad2deg(m_rotation(0)), Geometry::rad2deg(m_rotation(1)), Geometry::rad2deg(m_rotation(2))};
    char  buf[3][64];
    float buf_size[3];
    float vec_max = 0, unit_size = 0;
    for (int i = 0; i < 3; i++) {
        ImGui::DataTypeFormatString(buf[i], IM_ARRAYSIZE(buf[i]), ImGuiDataType_Double, (void *) &rotation[i], "%.2f");
        buf_size[i] = ImGui::CalcTextSize(buf[i]).x;
        vec_max     = std::max(buf_size[i], vec_max);
    }

    float buf_size_max = ImGui::CalcTextSize("-100.00").x;
    if (vec_max < buf_size_max) {
        unit_size = buf_size_max + ImGui::GetStyle().FramePadding.x * 2.0f;
    } else {
        unit_size = vec_max + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    ImGui::PushItemWidth(caption_size);
    ImGui::Dummy(ImVec2(caption_size, -1));
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("X");
    ImGui::SameLine(caption_size + 1 * unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Y");
    ImGui::SameLine(caption_size + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Z");

    ImGui::AlignTextToFramePadding();

    // Rotation input box
    ImGui::PushItemWidth(caption_size);
    m_imgui->text(_L("Rotation") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_x", &rotation[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_size + 1 * unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_y", &rotation[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_size + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_z", &rotation[2], 0.0f, 0.0f, "%.2f");
    if (current_active_id != m_last_active_id) {
        if (std::abs(Geometry::rad2deg(m_rotation(0)) - m_buffered_rotation(0)) > EPSILON || std::abs(Geometry::rad2deg(m_rotation(1)) - m_buffered_rotation(1)) > EPSILON ||
            std::abs(Geometry::rad2deg(m_rotation(2)) - m_buffered_rotation(2)) > EPSILON) {
            m_rotation = m_buffered_rotation;
            m_buffered_rotation.setZero();
            update_plane_points();
            m_parent.post_event(SimpleEvent(wxEVT_PAINT));
        }
    } else {
        m_buffered_rotation(0) = Geometry::deg2rad(rotation(0));
        m_buffered_rotation(1) = Geometry::deg2rad(rotation(1));
        m_buffered_rotation(2) = Geometry::deg2rad(rotation(2));
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 10.0f));
    // Movement input box
    double movement = m_movement;
    ImGui::PushItemWidth(caption_size);
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Movement") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(3 * unit_size + 2 * space_size);
    ImGui::BBLInputDouble("##cut_movement", &movement, 0.0f, 0.0f, "%.2f");
    if (current_active_id != m_last_active_id) {
        if (std::abs(m_buffered_movement - m_movement) > EPSILON) {
            m_movement          = m_buffered_movement;
            m_buffered_movement = 0.0;

            // update absolute height
            Vec3d plane_normal = get_plane_normal();
            m_height_delta     = plane_normal(2) * m_movement;
            m_height += m_height_delta;
            m_buffered_height = m_height;

            update_plane_points();
            m_parent.post_event(SimpleEvent(wxEVT_PAINT));
        }
    } else {
        m_buffered_movement = movement;
    }

    // height input box
    double height = m_height;
    ImGui::PushItemWidth(caption_size);
    ImGui::AlignTextToFramePadding();
    // only allow setting height when cut plane is horizontal
    Vec3d plane_normal = get_plane_normal();
    m_imgui->disabled_begin(std::abs(plane_normal(0)) > EPSILON || std::abs(plane_normal(1)) > EPSILON);
    m_imgui->text(_L("Height") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(3 * unit_size + 2 * space_size);
    ImGui::BBLInputDouble("##cut_height", &height, 0.0f, 0.0f, "%.2f");
    if (current_active_id != m_last_active_id) {
        if (std::abs(m_buffered_height - m_height) > EPSILON) {
            m_height_delta = m_buffered_height - m_height;
            m_height       = m_buffered_height;
            update_plane_points();
            m_parent.post_event(SimpleEvent(wxEVT_PAINT));
        }
    } else {
        m_buffered_height = height;
    }
    ImGui::PopStyleVar(1);
    m_imgui->disabled_end();

    m_imgui->disabled_begin(!m_keep_upper || !m_keep_lower);
    if (m_imgui->button(_L("Add/Edit connectors"))) set_connectors_editing(true);
    m_imgui->disabled_end();

    ImGui::Separator();

    float label_width = 0;
    for (const wxString& label : {_L("Upper part"), _L("Lower part")}) {
        const float width = m_imgui->calc_text_size(label).x + m_imgui->scaled(1.5f);
        if (label_width < width)
            label_width = width;
    }

    auto render_part_action_line = [this, label_width](const wxString& label, const wxString& suffix, bool& keep_part, bool& place_on_cut_part, bool& rotate_part) {
        CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

        bool keep = true;
        ImGui::AlignTextToFramePadding();
        m_imgui->text(label);

        ImGui::SameLine(label_width);

        m_imgui->disabled_begin(!connectors.empty());
        m_imgui->bbl_checkbox(_L("Keep") + suffix, connectors.empty() ? keep_part : keep);
        m_imgui->disabled_end();

        ImGui::SameLine();

        m_imgui->disabled_begin(!keep_part);
        if (m_imgui->bbl_checkbox(_L("Place on cut") + suffix, place_on_cut_part))
            rotate_part = false;
        ImGui::SameLine();
        if (m_imgui->bbl_checkbox(_L("Flip") + suffix, rotate_part))
            place_on_cut_part = false;
        m_imgui->disabled_end();
    };

    m_imgui->text(_L("After cut") + ": ");
    render_part_action_line( _L("Upper part"), "##upper", m_keep_upper, m_place_on_cut_upper, m_rotate_upper);
    render_part_action_line( _L("Lower part"), "##lower", m_keep_lower, m_place_on_cut_lower, m_rotate_lower);


#if 0
    // Auto segment input
    ImGui::PushItemWidth(m_imgui->get_style_scaling() * 150.0);
    m_imgui->checkbox(_L("Auto Segment"), m_do_segment);
    m_imgui->disabled_begin(!m_do_segment);
    ImGui::InputDouble("smoothing_alpha", &m_segment_smoothing_alpha, 0.0f, 0.0f, "%.2f");
    m_segment_smoothing_alpha = std::max(0.1, std::min(100.0, m_segment_smoothing_alpha));
    ImGui::InputInt("segment number", &m_segment_number);
    m_segment_number = std::max(1, m_segment_number);
    m_imgui->disabled_end();

    ImGui::Separator();
#endif

    // Cut button
    m_imgui->disabled_begin(!can_perform_cut());
    if (m_imgui->button(_L("Perform cut")))
        perform_cut(m_parent.get_selection());
    m_imgui->disabled_end();
    ImGui::SameLine();
    const bool reset_clicked = m_imgui->button(_L("Reset"));
    if (reset_clicked) { reset_all(); }

    m_last_active_id = current_active_id;
}

void GLGizmoAdvancedCut::init_connectors_input_window_data()
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

    m_label_width    = m_imgui->get_font_size() * 6.f;
    m_control_width  = m_imgui->get_font_size() * 9.f;

    if (m_connectors_editing && m_selected_count > 0) {
        float             depth_ratio           {UndefFloat};
        float             depth_ratio_tolerance {UndefFloat};
        float             radius                {UndefFloat};
        float             radius_tolerance      {UndefFloat};
        CutConnectorType  type{CutConnectorType::Undef};
        CutConnectorStyle style{CutConnectorStyle::Undef};
        CutConnectorShape shape{CutConnectorShape::Undef};

        bool is_init = false;
        for (size_t idx = 0; idx < m_selected.size(); idx++)
            if (m_selected[idx]) {
                const CutConnector &connector = connectors[idx];
                if (!is_init) {
                    depth_ratio           = connector.height;
                    depth_ratio_tolerance = connector.height_tolerance;
                    radius                = connector.radius;
                    radius_tolerance      = connector.radius_tolerance;
                    type                  = connector.attribs.type;
                    style                 = connector.attribs.style;
                    shape                 = connector.attribs.shape;

                    if (m_selected_count == 1) break;
                    is_init = true;
                } else {
                    if (!is_approx(depth_ratio, connector.height))
                        depth_ratio = UndefFloat;
                    if (!is_approx(depth_ratio_tolerance, connector.height_tolerance))
                        depth_ratio_tolerance = UndefFloat;
                    if (!is_approx(radius, connector.radius))
                        radius = UndefFloat;
                    if (!is_approx(radius_tolerance, connector.radius_tolerance))
                        radius_tolerance = UndefFloat;

                    if (type != connector.attribs.type)
                        type = CutConnectorType::Undef;
                    if (style != connector.attribs.style)
                        style = CutConnectorStyle::Undef;
                    if (shape != connector.attribs.shape)
                        shape = CutConnectorShape::Undef;
                }
            }

        m_connector_depth_ratio           = depth_ratio;
        m_connector_depth_ratio_tolerance = depth_ratio_tolerance;
        m_connector_size                  = 2.f * radius;
        m_connector_size_tolerance        = radius_tolerance;
        m_connector_type                  = type;
        m_connector_style                 = size_t(style);
        m_connector_shape_id              = size_t(shape);
    }
}

void GLGizmoAdvancedCut::render_connectors_input_window(float x, float y, float bottom_limit)
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

    // update when change input window
    m_imgui->set_requires_extra_frame();

    ImGui::AlignTextToFramePadding();
    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _L("Connectors"));

    m_imgui->disabled_begin(connectors.empty());
    ImGui::SameLine(m_label_width);
    if (render_reset_button("connectors", _u8L("Remove connectors")))
        reset_connectors();
    m_imgui->disabled_end();

    m_imgui->text(_L("Type"));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
    bool type_changed = render_connect_type_radio_button(CutConnectorType::Plug);
    type_changed |= render_connect_type_radio_button(CutConnectorType::Dowel);
    if (type_changed)
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.type = CutConnectorType(m_connector_type); });
    ImGui::PopStyleColor(1);

    std::vector<std::string> connector_styles = {_u8L("Prizm"), _u8L("Frustum")};
    std::vector<std::string> connector_shapes = { _u8L("Triangle"), _u8L("Square"), _u8L("Hexagon"), _u8L("Circle") };

    m_imgui->disabled_begin(m_connector_type == CutConnectorType::Dowel);
    if (type_changed && m_connector_type == CutConnectorType::Dowel) {
        m_connector_style = size_t(CutConnectorStyle::Prizm);
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.style = CutConnectorStyle(m_connector_style); });
    }

    ImGuiWrapper::push_combo_style(m_parent.get_scale());
    if (render_combo(_u8L("Style"), connector_styles, m_connector_style))
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.style = CutConnectorStyle(m_connector_style); });
    m_imgui->disabled_end();

    if (render_combo(_u8L("Shape"), connector_shapes, m_connector_shape_id))
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.shape = CutConnectorShape(m_connector_shape_id); });
    ImGuiWrapper::pop_combo_style();

    if (render_slider_double_input(_u8L("Depth ratio"), m_connector_depth_ratio, m_connector_depth_ratio_tolerance))
        apply_selected_connectors([this, &connectors](size_t idx) {
            if (m_connector_depth_ratio > 0)
                connectors[idx].height = m_connector_depth_ratio;
            if (m_connector_depth_ratio_tolerance >= 0)
                connectors[idx].height_tolerance = m_connector_depth_ratio_tolerance;
        });

    if (render_slider_double_input(_u8L("Size"), m_connector_size, m_connector_size_tolerance))
        apply_selected_connectors([this, &connectors](size_t idx) {
            if (m_connector_size > 0)
                connectors[idx].radius = 0.5f * m_connector_size;
            if (m_connector_size_tolerance >= 0)
                connectors[idx].radius_tolerance = m_connector_size_tolerance;
        });

    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(x, get_cur_y);

    float f_scale = m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine();
    if (m_imgui->button(_L("Confirm connectors"))) {
        unselect_all_connectors();
        set_connectors_editing(false);
    }

    ImGui::SameLine(2.75f * m_label_width);

    if (m_imgui->button(_L("Cancel"))) {
        reset_connectors();
        set_connectors_editing(false);
    }

    ImGui::PopStyleVar(2);
}

void GLGizmoAdvancedCut::render_input_window_warning() const
{
    if (m_has_invalid_connector) {
        wxString out = /*wxString(ImGui::WarningMarkerSmall)*/ _L("Warning") + ": " + _L("Invalid connectors detected") + ":";
        if (m_info_stats.outside_cut_contour > size_t(0))
            out += "\n - " + std::to_string(m_info_stats.outside_cut_contour) +
                   (m_info_stats.outside_cut_contour == 1 ? _L("connector is out of cut contour") : _L("connectors are out of cut contour"));
        if (m_info_stats.outside_bb > size_t(0))
            out += "\n - " + std::to_string(m_info_stats.outside_bb) +
                   (m_info_stats.outside_bb == 1 ? _L("connector is out of object") : _L("connectors is out of object"));
        if (m_info_stats.is_overlap)
            out += "\n - " + _L("Some connectors are overlapped");
        m_imgui->text(out);
    }
    if (!m_keep_upper && !m_keep_lower) m_imgui->text(/*wxString(ImGui::WarningMarkerSmall)*/_L("Warning") + ": " + _L("Invalid state. \nNo one part is selected for keep after cut"));
}

bool GLGizmoAdvancedCut::render_reset_button(const std::string &label_id, const std::string &tooltip) const
{
    const ImGuiStyle &style = ImGui::GetStyle();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1, style.ItemSpacing.y});

    ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.25f, 0.25f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.4f, 0.4f, 0.4f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.4f, 0.4f, 0.4f, 1.0f});

    bool revert = m_imgui->button(wxString(ImGui::RevertBtn));

    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(tooltip.c_str(), ImGui::GetFontSize() * 20.0f);

    ImGui::PopStyleVar();

    return revert;
}

bool GLGizmoAdvancedCut::render_connect_type_radio_button(CutConnectorType type)
{
    ImGui::SameLine(type == CutConnectorType::Plug ? m_label_width : 2 * m_label_width);
    ImGui::PushItemWidth(m_control_width);

    wxString radio_name;
    switch (type) {
    case CutConnectorType::Plug:
        radio_name = _L("Plug");
        break;
    case CutConnectorType::Dowel:
        radio_name = _L("Dowel");
        break;
    default:
        break;
    }

    if (m_imgui->radio_button(radio_name, m_connector_type == type)) {
        m_connector_type = type;
        update_connector_shape();
        return true;
    }
    return false;
}

bool GLGizmoAdvancedCut::render_combo(const std::string &label, const std::vector<std::string> &lines, size_t &selection_idx)
{
    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(m_label_width);
    ImGui::PushItemWidth(m_control_width);

    size_t selection_out = selection_idx;
    // It is necessary to use BeginGroup(). Otherwise, when using SameLine() is called, then other items will be drawn inside the combobox.
    ImGui::BeginGroup();
    ImVec2 combo_pos = ImGui::GetCursorScreenPos();
    if (ImGui::BeginCombo(("##" + label).c_str(), "")) {
        for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
            ImGui::PushID(int(line_idx));
            if (ImGui::Selectable("", line_idx == selection_idx)) selection_out = line_idx;

            ImGui::SameLine();
            ImGui::Text("%s", lines[line_idx].c_str());
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    ImVec2      backup_pos = ImGui::GetCursorScreenPos();
    ImGuiStyle &style      = ImGui::GetStyle();

    ImGui::SetCursorScreenPos(ImVec2(combo_pos.x + style.FramePadding.x, combo_pos.y + style.FramePadding.y));
    std::string str_line = selection_out < lines.size() ? lines[selection_out] : " ";
    ImGui::Text("%s", str_line.c_str());
    ImGui::SetCursorScreenPos(backup_pos);
    ImGui::EndGroup();

    bool is_changed = selection_idx != selection_out;
    selection_idx   = selection_out;

    if (is_changed) update_connector_shape();

    return is_changed;
}

bool GLGizmoAdvancedCut::render_slider_double_input(const std::string &label, float &value_in, float &tolerance_in)
{
    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(m_label_width);
    ImGui::PushItemWidth(m_control_width * 0.85f);

    bool m_imperial_units = false;

    float value = value_in;
    if (m_imperial_units) value *= float(units_mm_to_in);
    float old_val = value;

    constexpr float UndefMinVal = -0.1f;

    const BoundingBoxf3 bbox      = bounding_box();
    float               mean_size = float((bbox.size().x() + bbox.size().y() + bbox.size().z()) / 9.0);
    float               min_size  = value_in < 0.f ? UndefMinVal : 2.f;
    if (m_imperial_units) {
        mean_size *= float(units_mm_to_in);
        min_size *= float(units_mm_to_in);
    }
    std::string format = value_in < 0.f ? " " : m_imperial_units ? "%.4f  " + _u8L("in") : "%.2f  " + _u8L("mm");

    m_imgui->slider_float(("##" + label).c_str(), &value, min_size, mean_size, format.c_str());
    value_in = value * float(m_imperial_units ? units_in_to_mm : 1.0);

    ImGui::SameLine(m_label_width + m_control_width + 3);
    ImGui::PushItemWidth(m_control_width * 0.3f);

    float       old_tolerance, tolerance = old_tolerance = tolerance_in * 100.f;
    std::string format_t      = tolerance_in < 0.f ? " " : "%.f %%";
    float       min_tolerance = tolerance_in < 0.f ? UndefMinVal : 0.f;

    m_imgui->slider_float(("##tolerance_" + label).c_str(), &tolerance, min_tolerance, 20.f, format_t.c_str(), 1.f, true, _L("Tolerance"));
    tolerance_in = tolerance * 0.01f;

    return !is_approx(old_val, value) || !is_approx(old_tolerance, tolerance);
}

bool GLGizmoAdvancedCut::cut_line_processing() const
{
    return m_cut_line_begin != Vec3d::Zero();
}

void GLGizmoAdvancedCut::discard_cut_line_processing()
{
    m_cut_line_begin = m_cut_line_end = Vec3d::Zero();
}

bool GLGizmoAdvancedCut::process_cut_line(SLAGizmoEventType action, const Vec2d &mouse_position)
{
    const Camera &camera = wxGetApp().plater()->get_camera();

    Vec3d pt;
    Vec3d dir;
    MeshRaycaster::line_from_mouse_pos_static(mouse_position, Transform3d::Identity(), camera, pt, dir);
    dir.normalize();
    pt += dir; // Move the pt along dir so it is not clipped.

    if (action == SLAGizmoEventType::LeftDown && !cut_line_processing()) {
        m_cut_line_begin = pt;
        m_cut_line_end = pt;
        return true;
    }

    if (cut_line_processing()) {
        m_cut_line_end = pt;
        if (action == SLAGizmoEventType::LeftDown || action == SLAGizmoEventType::LeftUp) {
            Vec3d line_dir = m_cut_line_end - m_cut_line_begin;
            if (line_dir.norm() < 3.0)
                return true;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Cut by line");

            Vec3d              cross_dir = line_dir.cross(dir).normalized();
            Eigen::Quaterniond q;
            Transform3d        m         = Transform3d::Identity();
            m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(m_cut_plane_normal, cross_dir).toRotationMatrix();

            m_rotate_matrix = m;

            const ModelObject *  mo = m_c->selection_info()->model_object();
            const ModelInstance *mi = mo->instances[m_c->selection_info()->get_active_instance()];
            Vec3d plane_center = get_plane_center();

            auto update_plane_after_line_cut = [this](const Vec3d &deta_plane_center, const Transform3d& rotate_matrix) {
                Vec3d plane_center = get_plane_center();

                std::array<Vec3d, 4> plane_points_rot;
                for (int i = 0; i < plane_points_rot.size(); i++) {
                    plane_points_rot[i] = m_cut_plane_points[i] - plane_center;
                }

                for (uint32_t i = 0; i < plane_points_rot.size(); ++i) {
                    plane_points_rot[i] = rotate_matrix * plane_points_rot[i];
                }

                for (int i = 0; i < plane_points_rot.size(); i++) {
                    m_cut_plane_points[i] = plane_points_rot[i] + plane_center + deta_plane_center;
                }
            };

            update_plane_after_line_cut(cross_dir * (cross_dir.dot(pt - m_cut_plane_center)), m_rotate_matrix);

            discard_cut_line_processing();
        } else if (action == SLAGizmoEventType::Dragging)
            this->set_dirty();
        return true;
    }
    return false;
}

} // namespace GUI
} // namespace Slic3r
