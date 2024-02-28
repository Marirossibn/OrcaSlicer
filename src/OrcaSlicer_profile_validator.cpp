#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/program_options.hpp>
#include <iostream>

using namespace Slic3r;
namespace po = boost::program_options;

int main(int argc, char* argv[])
{
    po::options_description desc("Allowed options");
    desc.add_options()("help,h", "help")
        ("path,p", po::value<std::string>()->default_value("../../../resources/profiles"), "profile folder")
        ("vendor,v", po::value<std::string>()->default_value(""),
            "Vendor name. Optional, all profiles present in the folder will be validated if not specified")
        ("log_level,l", po::value<int>()->default_value(2),
            "Log level. Optional, default is 2 (warning). Higher values produce more detailed logs.");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            std::cout << desc << "\n";
            return 1;
        }

        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << desc << "\n";
        return 1;
    }

    std::string path      = vm["path"].as<std::string>();
    std::string vendor    = vm["vendor"].as<std::string>();
    int         log_level = vm["log_level"].as<int>();

    //  check if path is valid, and return error if not
    if (!fs::exists(path) || !fs::is_directory(path)) {
        std::cerr << "Error: " << path << " is not a valid directory\n";
        return 1;
    }


    // std::cout<<"path: "<<path<<std::endl;
    // std::cout<<"vendor: "<<vendor<<std::endl;
    // std::cout<<"log_level: "<<log_level<<std::endl;

    set_data_dir(path);
    set_logging_level(log_level);
    auto preset_bundle = new PresetBundle();
    preset_bundle->setup_directories();
    preset_bundle->set_is_validation_mode(true);
    preset_bundle->set_vendor_to_validate(vendor);

    preset_bundle->set_default_suppressed(true);
    AppConfig app_config;

    try {
        auto preset_substitutions = preset_bundle->load_presets(app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what();
        std::cout << "Validation failed" << std::endl;
        return 1;
    }
    std::cout << "Validation completed successfully" << std::endl;
    return 0;
}
