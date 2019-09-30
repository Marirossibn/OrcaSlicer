#include "libslic3r/libslic3r.h"
#include "Mouse3DController.hpp"

#if ENABLE_3DCONNEXION_DEVICES

#include "GLCanvas3D.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"

#include <wx/glcanvas.h>

// WARN: If updating these lists, please also update resources/udev/90-3dconnexion.rules

static const std::vector<int> _3DCONNEXION_VENDORS =
{
    0x046d,  // LOGITECH = 1133 // Logitech (3Dconnexion is made by Logitech)
    0x256F   // 3DCONNECTION = 9583 // 3Dconnexion
};

static const std::vector<int> _3DCONNEXION_DEVICES =
{
    0xC623, // TRAVELER = 50723
    0xC626, // NAVIGATOR = 50726
    0xc628,	// NAVIGATOR_FOR_NOTEBOOKS = 50728
    0xc627, // SPACEEXPLORER = 50727
    0xC603, // SPACEMOUSE = 50691
    0xC62B, // SPACEMOUSEPRO = 50731
    0xc621, // SPACEBALL5000 = 50721
    0xc625, // SPACEPILOT = 50725
    0xc629  // SPACEPILOTPRO = 50729
};

static const unsigned int _3DCONNEXION_BUTTONS_COUNT = 2;

namespace Slic3r {
namespace GUI {
    
const double Mouse3DController::State::DefaultTranslationScale = 2.5;
const float Mouse3DController::State::DefaultRotationScale = 1.0;

Mouse3DController::State::State()
    : m_translation(Vec3d::Zero())
    , m_rotation(Vec3f::Zero())
    , m_buttons(_3DCONNEXION_BUTTONS_COUNT, false)
    , m_translation_scale(DefaultTranslationScale)
    , m_rotation_scale(DefaultRotationScale)
{
}

void Mouse3DController::State::set_translation(const Vec3d& translation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_translation = translation;
}

void Mouse3DController::State::set_rotation(const Vec3f& rotation)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rotation = rotation;
}

void Mouse3DController::State::set_button(unsigned int id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id < _3DCONNEXION_BUTTONS_COUNT)
        m_buttons[id] = true;
}

bool Mouse3DController::State::has_translation() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_translation.isApprox(Vec3d::Zero());
}
bool Mouse3DController::State::has_rotation() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_rotation.isApprox(Vec3f::Zero());
}

bool Mouse3DController::State::has_any_button() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < _3DCONNEXION_BUTTONS_COUNT; ++i)
    {
        if (m_buttons[i])
            return true;
    }
    return false;
}

bool Mouse3DController::State::apply(GLCanvas3D& canvas)
{
    if (!wxGetApp().IsActive())
        return false;

    bool ret = false;
    Camera& camera = canvas.get_camera();

    if (has_translation())
    {
        camera.set_target(camera.get_target() + m_translation_scale * (m_translation(0) * camera.get_dir_right() + m_translation(1) * camera.get_dir_forward() + m_translation(2) * camera.get_dir_up()));
        m_translation = Vec3d::Zero();
        ret = true;
    }

    if (has_rotation())
    {
        float theta = m_rotation_scale * m_rotation(0);
        float phi = m_rotation_scale * m_rotation(2);
        float sign = camera.inverted_phi ? -1.0f : 1.0f;
        camera.phi += sign * phi;
        camera.set_theta(camera.get_theta() + theta, wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA);
        m_rotation = Vec3f::Zero();
        ret = true;
    }

    if (has_any_button())
    {
        if (m_buttons[0])
            canvas.set_camera_zoom(1.0);
        else if (m_buttons[1])
            canvas.set_camera_zoom(-1.0);

        m_buttons = std::vector<bool>(_3DCONNEXION_BUTTONS_COUNT, false);
        ret = true;
    }

    return ret;
}

Mouse3DController::Mouse3DController()
    : m_initialized(false)
    , m_canvas(nullptr)
    , m_device(nullptr)
    , m_running(false)
{
}

void Mouse3DController::init()
{
    if (m_initialized)
        return;

    // Initialize the hidapi library
    int res = hid_init();
    if (res != 0)
        return;

    m_initialized = true;

    connect_device();
    start();
}

void Mouse3DController::shutdown()
{
    if (!m_initialized)
        return;

    stop();

    if (m_thread.joinable())
        m_thread.join();

    disconnect_device();

    // Finalize the hidapi library
    hid_exit();
    m_initialized = false;
}

void Mouse3DController::connect_device()
{
    if (m_device != nullptr)
        return;

    // Enumerates devices
    hid_device_info* devices = hid_enumerate(0, 0);
    if (devices == nullptr)
        return;

    // Searches for 1st connected 3Dconnexion device
    unsigned short vendor_id = 0;
    unsigned short product_id = 0;

    hid_device_info* current = devices;
    while (current != nullptr)
    {
        for (size_t i = 0; i < _3DCONNEXION_VENDORS.size(); ++i)
        {
            if (_3DCONNEXION_VENDORS[i] == current->vendor_id)
            {
                vendor_id = current->vendor_id;
                break;
            }
        }

        if (vendor_id != 0)
        {
            for (size_t i = 0; i < _3DCONNEXION_DEVICES.size(); ++i)
            {
                if (_3DCONNEXION_DEVICES[i] == current->product_id)
                {
                    product_id = current->product_id;
                    break;
                }
            }

            if (product_id == 0)
                vendor_id = 0;
        }

        if (vendor_id != 0)
            break;

        current = current->next;
    }

    // Free enumerated devices
    hid_free_enumeration(devices);

    if (vendor_id == 0)
        return;

    // Open the 3Dconnexion device using the VID, PID
    m_device = hid_open(vendor_id, product_id, nullptr);
}

void Mouse3DController::disconnect_device()
{
    if (m_device == nullptr)
        return;
    
    // Close the 3Dconnexion device
    hid_close(m_device);
    m_device = nullptr;
}

void Mouse3DController::start()
{
    if ((m_device == nullptr) || m_running)
        return;

    m_thread = std::thread(&Mouse3DController::run, this);
}

void Mouse3DController::run()
{
    m_running = true;
    while (m_running)
    {
        collect_input();
    }
}

double convert_input(int first, unsigned char val)
{
    int ret = 0;

    switch (val)
    {
    case 0: { ret = first; break; }
    case 1: { ret = first + 255; break; }
    case 254: { ret = -511 + first; break; }
    case 255: { ret = -255 + first; break; }
    default: { break; }
    }

    return (double)ret / 349.0;
}

void Mouse3DController::collect_input()
{
    // Read data from device
    enum EDataType
    {
        Translation = 1,
        Rotation,
        Button
    };

    unsigned char retrieved_data[8] = { 0 };
    int res = hid_read_timeout(m_device, retrieved_data, sizeof(retrieved_data), 100);
    if (res < 0)
    {
        // An error occourred (device detached from pc ?)
        stop();
        return;
    }

    if (res > 0)
    {
        switch (retrieved_data[0])
        {
        case Translation:
            {
                Vec3d translation(-convert_input((int)retrieved_data[1], retrieved_data[2]),
                        convert_input((int)retrieved_data[3], retrieved_data[4]),
                        convert_input((int)retrieved_data[5], retrieved_data[6]));
                if (!translation.isApprox(Vec3d::Zero()))
                    m_state.set_translation(translation);

                break;
            }
        case Rotation:
            {
                Vec3f rotation(-(float)convert_input((int)retrieved_data[1], retrieved_data[2]),
                    (float)convert_input((int)retrieved_data[3], retrieved_data[4]),
                    -(float)convert_input((int)retrieved_data[5], retrieved_data[6]));
                if (!rotation.isApprox(Vec3f::Zero()))
                    m_state.set_rotation(rotation);

                break;
            }
        case Button:
            {
                for (unsigned int i = 0; i < _3DCONNEXION_BUTTONS_COUNT; ++i)
                {
                    if (retrieved_data[1] & (0x1 << i))
                        m_state.set_button(i);
                }

                break;
            }
        default:
            break;
        }
    }
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_3DCONNEXION_DEVICES
