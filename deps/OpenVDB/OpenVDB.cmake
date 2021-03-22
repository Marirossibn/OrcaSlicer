if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

prusaslicer_add_cmake_project(OpenVDB
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openvdb.git
    GIT_TAG aebaf8d95be5e57fd33949281ec357db4a576c2e #v6.2.1
    DEPENDS dep_TBB dep_Blosc dep_OpenEXR #dep_Boost
    PATCH_COMMAND       ${GIT_EXECUTABLE} reset --hard && ${GIT_EXECUTABLE} clean -df &&
                        ${GIT_EXECUTABLE} apply --whitespace=nowarn ${CMAKE_CURRENT_LIST_DIR}/openvdb-mods.patch
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON 
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF
        -DUSE_BLOSC=ON
        -DOPENVDB_CORE_SHARED=${_build_shared} 
        -DOPENVDB_CORE_STATIC=${_build_static}
        -DOPENVDB_ENABLE_RPATH:BOOL=OFF
        -DTBB_STATIC=${_build_static}
        -DOPENVDB_BUILD_VDB_PRINT=ON
        -DDISABLE_DEPENDENCY_VERSION_CHECKS=ON # Centos6 has old zlib
)

if (MSVC)
    if (${DEP_DEBUG})
        ExternalProject_Get_Property(dep_OpenVDB BINARY_DIR)
        ExternalProject_Add_Step(dep_OpenVDB build_debug
            DEPENDEES build
            DEPENDERS install
            COMMAND ${CMAKE_COMMAND} ../dep_OpenVDB -DOPENVDB_BUILD_VDB_PRINT=OFF
            COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
            WORKING_DIRECTORY "${BINARY_DIR}"
        )
    endif ()
endif ()