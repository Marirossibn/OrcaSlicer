// Why?
#define _WIN32_WINNT 0x0502
// The standard Windows includes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <wchar.h>
// Let the NVIDIA and AMD know we want to use their graphics card
// on a dual graphics card system.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

#include <stdlib.h>
#include <stdio.h>
#include <GL/GL.h>

#include <string>
#include <vector>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

class OpenGLVersionCheck
{
public:
	std::string version;
	std::string glsl_version;
	std::string vendor;
	std::string renderer;

	HINSTANCE   hOpenGL = nullptr;
	bool 		success = false;

	bool load_opengl_dll()
	{
	    MSG      msg     = {0};
	    WNDCLASS wc      = {0}; 
	    wc.lpfnWndProc   = OpenGLVersionCheck::supports_opengl2_wndproc;
	    wc.hInstance     = (HINSTANCE)GetModuleHandle(nullptr);
	    wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
	    wc.lpszClassName = L"slic3r_opengl_version_check";
	    wc.style = CS_OWNDC;
	    if (RegisterClass(&wc)) {
			HWND hwnd = CreateWindowW(wc.lpszClassName, L"slic3r_opengl_version_check", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, 0, 0, wc.hInstance, (LPVOID)this);
			if (hwnd) {
				this->message_pump_exit = false;
			    while (GetMessage(&msg, NULL, 0, 0 ) > 0 && ! this->message_pump_exit)
			        DispatchMessage(&msg);
			}
		}
	    return this->success;
	}

	void unload_opengl_dll() 
	{
		if (this->hOpenGL) {
			FreeLibrary(this->hOpenGL);
			this->hOpenGL = nullptr;
		}
	}

	bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
	{
	    std::vector<std::string> tokens;
	    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);
	    if (tokens.empty())
	        return false;

	    std::vector<std::string> numbers;
	    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

	    unsigned int gl_major = 0;
	    unsigned int gl_minor = 0;
	    if (numbers.size() > 0)
	        gl_major = ::atoi(numbers[0].c_str());
	    if (numbers.size() > 1)
	        gl_minor = ::atoi(numbers[1].c_str());
	    if (gl_major < major)
	        return false;
	    else if (gl_major > major)
	        return true;
	    else
	        return gl_minor >= minor;
	}

protected:
	bool message_pump_exit = false;

	void check(HWND hWnd)
	{
		hOpenGL = LoadLibraryExW(L"opengl32.dll", nullptr, 0);
		if (hOpenGL == nullptr) {
			printf("Failed loading the system opengl32.dll\n");
			return;
		}

		typedef HGLRC 		(WINAPI *Func_wglCreateContext)(HDC);
		typedef BOOL 		(WINAPI *Func_wglMakeCurrent  )(HDC, HGLRC);
		typedef BOOL     	(WINAPI *Func_wglDeleteContext)(HGLRC);
		typedef GLubyte* 	(WINAPI *Func_glGetString     )(GLenum);

		Func_wglCreateContext 	wglCreateContext = (Func_wglCreateContext)GetProcAddress(hOpenGL, "wglCreateContext");
		Func_wglMakeCurrent 	wglMakeCurrent 	 = (Func_wglMakeCurrent)  GetProcAddress(hOpenGL, "wglMakeCurrent");
		Func_wglDeleteContext 	wglDeleteContext = (Func_wglDeleteContext)GetProcAddress(hOpenGL, "wglDeleteContext");
		Func_glGetString 		glGetString 	 = (Func_glGetString)	  GetProcAddress(hOpenGL, "glGetString");

		if (wglCreateContext == nullptr || wglMakeCurrent == nullptr || wglDeleteContext == nullptr || glGetString == nullptr) {
			printf("Failed loading the system opengl32.dll: The library is invalid.\n");
			return;
		}

        PIXELFORMATDESCRIPTOR pfd =
        {
            sizeof(PIXELFORMATDESCRIPTOR),
            1,
            PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA,            	// The kind of framebuffer. RGBA or palette.
            32,                        	// Color depth of the framebuffer.
            0, 0, 0, 0, 0, 0,
            0,
            0,
            0,
            0, 0, 0, 0,
            24,                        	// Number of bits for the depthbuffer
            8,                        	// Number of bits for the stencilbuffer
            0,                        	// Number of Aux buffers in the framebuffer.
            PFD_MAIN_PLANE,
            0,
            0, 0, 0
        };

        HDC ourWindowHandleToDeviceContext = ::GetDC(hWnd);
        // Gdi32.dll
        int letWindowsChooseThisPixelFormat = ::ChoosePixelFormat(ourWindowHandleToDeviceContext, &pfd); 
        // Gdi32.dll
        SetPixelFormat(ourWindowHandleToDeviceContext,letWindowsChooseThisPixelFormat, &pfd);
        // Opengl32.dll
        HGLRC glcontext = wglCreateContext(ourWindowHandleToDeviceContext);
        wglMakeCurrent(ourWindowHandleToDeviceContext, glcontext);
        // Opengl32.dll
	    const char *data = (const char*)glGetString(GL_VERSION);
	    if (data != nullptr)
	        this->version = data;
		data = (const char*)glGetString(0x8B8C); // GL_SHADING_LANGUAGE_VERSION
    	if (data != nullptr)
        	this->glsl_version = data;
    	data = (const char*)glGetString(GL_VENDOR);
    	if (data != nullptr)
        	this->vendor = data;
    	data = (const char*)glGetString(GL_RENDERER);
    	if (data != nullptr)
        	this->renderer = data;
        // Opengl32.dll
        wglDeleteContext(glcontext);
        this->success = true;
	}

	static LRESULT CALLBACK supports_opengl2_wndproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
	    switch(message)
	    {
	    case WM_CREATE:
		{
			CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
			OpenGLVersionCheck *ogl_data = reinterpret_cast<OpenGLVersionCheck*>(pCreate->lpCreateParams);
			ogl_data->check(hWnd);
			DestroyWindow(hWnd);
			ogl_data->message_pump_exit = true;
			return 0;
	    }
	    default:
	        return DefWindowProc(hWnd, message, wParam, lParam);
	    }
	}
};

extern "C" {
	typedef int (__stdcall *Slic3rMainFunc)(int argc, wchar_t **argv);
	Slic3rMainFunc slic3r_main = nullptr;
}

#ifdef SLIC3R_WRAPPER_NOCONSOLE
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, wchar_t *lpCmdLine, int nCmdShow)
{
	int 	  argc;
	wchar_t **argv = CommandLineToArgvW(lpCmdLine, &argc);
#else
int wmain(int argc, wchar_t **argv)
{
#endif

	OpenGLVersionCheck opengl_version_check;
	bool load_mesa = ! opengl_version_check.load_opengl_dll() || ! opengl_version_check.is_version_greater_or_equal_to(2, 0);

	wchar_t path_to_exe[MAX_PATH + 1] = { 0 };
	::GetModuleFileNameW(nullptr, path_to_exe, MAX_PATH);
	wchar_t drive[_MAX_DRIVE];
	wchar_t dir[_MAX_DIR];
	wchar_t fname[_MAX_FNAME];
	wchar_t ext[_MAX_EXT];
	_wsplitpath(path_to_exe, drive, dir, fname, ext);
	_wmakepath(path_to_exe, drive, dir, nullptr, nullptr);

// https://wiki.qt.io/Cross_compiling_Mesa_for_Windows
// http://download.qt.io/development_releases/prebuilt/llvmpipe/windows/
	if (load_mesa) {
		opengl_version_check.unload_opengl_dll();
		wchar_t path_to_mesa[MAX_PATH + 1] = { 0 };
		wcscpy(path_to_mesa, path_to_exe);
		wcscat(path_to_mesa, L"mesa\\opengl32.dll");
		printf("Loading MESA OpenGL library: %S\n", path_to_mesa);
		HINSTANCE hInstance_OpenGL = LoadLibraryExW(path_to_mesa, nullptr, 0);
		if (hInstance_OpenGL == nullptr) {
			printf("MESA OpenGL library was not loaded\n");
		}
	}

	wchar_t path_to_slic3r[MAX_PATH + 1] = { 0 };
	wcscpy(path_to_slic3r, path_to_exe);
	wcscat(path_to_slic3r, L"slic3r.dll");
//	printf("Loading Slic3r library: %S\n", path_to_slic3r);
	HINSTANCE hInstance_Slic3r = LoadLibraryExW(path_to_slic3r, nullptr, 0);
	if (hInstance_Slic3r == nullptr) {
		printf("slic3r.dll was not loaded\n");
		return -1;
	}

	// resolve function address here
	slic3r_main = (Slic3rMainFunc)GetProcAddress(hInstance_Slic3r, "slic3r_main");
	if (slic3r_main == nullptr) {
		printf("could not locate the function slic3r_main in slic3r.dll\n");
		return -1;
	}

	std::vector<wchar_t*> argv_extended;
	argv_extended.emplace_back(argv[0]);
#ifdef SLIC3R_WRAPPER_GUI
	std::wstring cmd_gui = L"--gui";
	argv_extended.emplace_back(const_cast<wchar_t*>(cmd_gui.data()));
#endif
	for (int i = 1; i < argc; ++ i)
		argv_extended.emplace_back(argv[i]);
	argv_extended.emplace_back(nullptr);
	return slic3r_main(argc, argv_extended.data());
}
