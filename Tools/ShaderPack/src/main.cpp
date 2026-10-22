// Packs a folder of shader sources into shader.h + shader.cpp, so that the shaders can
// be linked into the engine instead of shipped beside it.
//
// The output mirrors what the resource generators in other engines do: one blob holding
// every file back to back, a header with an OFFSET/SIZE/DATA macro trio per file, and a
// table the runtime looks a file up by name with. Nothing about the shaders themselves is
// interpreted here - this step only moves bytes - so it stays valid while the shader
// system is being reworked.
//
// Usage: ShaderPack -d <source dir> -o <output dir> [-n <package>] <relative file>...
//
// The relative files are the ones CMake globbed, so that a change to any of them
// rebuilds this. Without any, the folder is walked instead.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	struct Input
	{
		std::string name; // the name the runtime looks the blob up by, empty to derive it
		std::string path;
	};

	struct PackedFile
	{
		std::string name;     // the name inside the archive, '/'-separated
		std::string macro;    // NAME_OF_FILE, upper case
		std::vector<uint8_t> data;
	};

	std::string ToMacro(std::string p_name)
	{
		std::string macro;
		macro.reserve(p_name.size());
		for (char c : p_name)
		{
			const bool isNameCharacter = std::isalnum(static_cast<unsigned char>(c)) != 0;
			macro += isNameCharacter ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
		}
		return macro;
	}

	std::string ToSlashes(std::string p_path)
	{
		std::replace(p_path.begin(), p_path.end(), '\\', '/');
		return p_path;
	}

	bool ReadFile(const std::filesystem::path& p_path, std::vector<uint8_t>& p_out)
	{
		std::ifstream stream(p_path, std::ios::binary);
		if (!stream)
		{
			std::cerr << "[ShaderPack] cannot open " << p_path << std::endl;
			return false;
		}
		p_out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return true;
	}

	// Grows the byte array a line at a time: one long line of a few hundred thousand
	// initialisers has a habit of upsetting the compiler's line length limit.
	void WriteBlob(std::ostream& p_out, const std::vector<uint8_t>& p_bytes)
	{
		constexpr size_t kPerLine = 20;
		char buffer[8] = {};
		for (size_t i = 0; i < p_bytes.size(); ++i)
		{
			std::snprintf(buffer, sizeof(buffer), "0x%02x,", static_cast<int>(p_bytes[i]));
			p_out << buffer;
			if ((i + 1) % kPerLine == 0)
			{
				p_out << '\n';
			}
		}
		p_out << '\n';
	}
}

int main(int argc, char* argv[])
{
	std::string sourceDir;
	std::string outputDir;
	std::string package = "MOONSHADER";
	std::vector<Input> inputs;

	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];
		if (argument == "-d" && i + 1 < argc)
		{
			sourceDir = argv[++i];
		}
		else if (argument == "-o" && i + 1 < argc)
		{
			outputDir = argv[++i];
		}
		else if (argument == "-n" && i + 1 < argc)
		{
			package = argv[++i];
		}
		else if (!argument.empty() && argument[0] == '-')
		{
			std::cerr << "[ShaderPack] unknown option " << argument << std::endl;
			return 1;
		}
		else
		{
			// "name=path" names the blob explicitly, which is what the build passes: the
			// MoonMatc output for a shader lives in a different folder than the .glsl the
			// legacy shaders still #include, and both have to land under the name the
			// runtime asks for.
			const size_t equals = argument.find('=');
			if (equals != std::string::npos)
			{
				inputs.push_back({ argument.substr(0, equals), argument.substr(equals + 1) });
			}
			else
			{
				inputs.push_back({ {}, argument });
			}
		}
	}

	if (outputDir.empty())
	{
		std::cerr << "Usage: ShaderPack -o <output dir> [-n <package>] [name=]<path>..." << std::endl;
		return 1;
	}

	const std::filesystem::path root(sourceDir);
	const std::string packagePrefix = ToMacro(package) + "_";

	// Without an explicit list, walk the folder - the same files CMake would have globbed.
	if (inputs.empty())
	{
		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}
			const std::string extension = entry.path().extension().string();
			if (extension == ".shader" || extension == ".glsl")
			{
				inputs.push_back({
					ToSlashes(std::filesystem::relative(entry.path(), root).string()),
					entry.path().string() });
			}
		}
		std::sort(inputs.begin(), inputs.end(),
			[](const Input& a, const Input& b) { return a.name < b.name; });
	}

	std::vector<PackedFile> files;
	size_t totalSize = 0;
	for (const Input& input : inputs)
	{
		// The archived name comes from the caller when it gave one ("Standard.shader=.../matc/
		// Standard.shader"); otherwise it is the path relative to -d, which is what a plain
		// "ShaderPack -d <shaders> ..." run means.
		std::filesystem::path relative(input.path);
		if (!input.name.empty())
		{
			relative = input.name;
		}
		else if (!sourceDir.empty() && relative.is_absolute())
		{
			relative = std::filesystem::relative(relative, std::filesystem::path(sourceDir));
		}

		PackedFile file;
		file.name = ToSlashes(relative.string());
		file.macro = packagePrefix + ToMacro(file.name);
		if (!ReadFile(input.path, file.data))
		{
			return 1;
		}
		totalSize += file.data.size();
		files.push_back(std::move(file));
	}

	std::filesystem::create_directories(outputDir);
	const std::filesystem::path headerPath = std::filesystem::path(outputDir) / "shader.h";
	const std::filesystem::path sourcePath = std::filesystem::path(outputDir) / "shader.cpp";

	// ---------------------------------------------------------------- shader.h
	{
		std::ofstream header(headerPath);
		if (!header)
		{
			std::cerr << "[ShaderPack] cannot write " << headerPath << std::endl;
			return 1;
		}
		header << "// Generated by ShaderPack. Do not edit.\n"
			<< "//\n"
			<< "// The shader sources of this build, packed into one blob so that they are linked\n"
			<< "// into the executable instead of shipped in a folder beside it. The loaders look a\n"
			<< "// source up by name (Rendering/Resources/ShaderArchive.h) and never touch the disk\n"
			<< "// when the archive has it.\n"
			<< "#pragma once\n"
			<< "#include <cstdint>\n\n"
			<< "extern \"C\" {\n"
			<< "extern const uint8_t " << packagePrefix << "PACKAGE[];\n"
			<< "extern const uint32_t " << packagePrefix << "PACKAGE_SIZE;\n"
			<< "}\n\n"
			<< "#define " << packagePrefix << "FILE_COUNT " << files.size() << "u\n\n";

		uint32_t offset = 0;
		for (const PackedFile& file : files)
		{
			const uint32_t size = static_cast<uint32_t>(file.data.size());
			header << "// " << file.name << "\n"
				<< "#define " << file.macro << "_OFFSET " << offset << "u\n"
				<< "#define " << file.macro << "_SIZE " << size << "u\n"
				<< "#define " << file.macro << "_DATA (" << packagePrefix << "PACKAGE + "
				<< file.macro << "_OFFSET)\n\n";
			offset += size;
		}
	}

	// -------------------------------------------------------------- shader.cpp
	{
		std::ofstream source(sourcePath);
		if (!source)
		{
			std::cerr << "[ShaderPack] cannot write " << sourcePath << std::endl;
			return 1;
		}

		source << "// Generated by ShaderPack. Do not edit.\n"
			<< "#include \"shader.h\"\n"
			<< "#include <Rendering/Resources/ShaderArchive.h>\n\n"
			<< "extern \"C\" const uint8_t " << packagePrefix << "PACKAGE[] = {\n";
		for (const PackedFile& file : files)
		{
			WriteBlob(source, file.data);
		}
		source << "};\n\n"
			<< "extern \"C\" const uint32_t " << packagePrefix << "PACKAGE_SIZE = sizeof("
			<< packagePrefix << "PACKAGE);\n\n"
			<< "namespace Rendering::Resources\n{\n"
			<< "\tnamespace\n\t{\n"
			<< "\t\t// The table a lookup walks: every source this build packed, by the name it\n"
			<< "\t\t// has inside the shader folder.\n"
			<< "\t\tconst ShaderFile kShaderFiles[] = {\n";
		for (const PackedFile& file : files)
		{
			source << "\t\t\t{ \"" << file.name << "\", " << file.macro << "_DATA, "
				<< file.macro << "_SIZE },\n";
		}
		source << "\t\t};\n"
			<< "\t}\n\n"
			<< "\tconst ShaderArchive& GetShaderArchive()\n"
			<< "\t{\n"
			<< "\t\tstatic const ShaderArchive archive{ kShaderFiles, "
			<< packagePrefix << "FILE_COUNT };\n"
			<< "\t\treturn archive;\n"
			<< "\t}\n"
			<< "}\n";
	}

	std::cout << "[ShaderPack] packed " << files.size() << " shader sources (" << totalSize
		<< " bytes) into " << sourcePath << std::endl;
	return 0;
}
