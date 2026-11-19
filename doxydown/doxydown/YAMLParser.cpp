#include <doxydown/YAMLParser.hpp>

#include <cstddef>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

YAMLParser::YAMLParser() {
  yaml_parser_initialize(&parser);
}

YAMLParser::~YAMLParser() {
  yaml_parser_delete(&parser);
}

static int ifstream_read_handler(void* data, unsigned char* buffer, size_t size, size_t* size_read) {
    std::ifstream* stream = static_cast<std::ifstream*>(data);
    if (stream == nullptr || stream->bad()) {
      return 0; // error
    }
    stream->read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    if (stream->bad()) {
      *size_read = 0;
      return 0; // I/O error occurred during read()
    }
    *size_read = static_cast<size_t>(stream->gcount());
    return 1; // success (returns 1 with *size_read = 0 on EOF)
}

YAMLNode YAMLParser::parse(const std::filesystem::path& filename) {
  YAMLNode root;
  std::ifstream file(filename, std::ios::binary);
  if (not file.is_open()) {
    throw std::runtime_error("could not read YAML file " + filename.string());
  }
  yaml_parser_set_input(&parser, ifstream_read_handler, &file);
  bool done = false;
  while (not done) {
    if (!yaml_parser_parse(&parser, &event)) {
      throw std::runtime_error("YAML syntax error in file " +
          filename.string());
    }
    if (event.type == YAML_SEQUENCE_START_EVENT) {
      parseSequence(filename, root);
    } else if (event.type == YAML_MAPPING_START_EVENT) {
      parseMapping(filename, root);
    } else if (event.type == YAML_DOCUMENT_END_EVENT ||
        event.type == YAML_STREAM_END_EVENT) {
      done = true;
      yaml_event_delete(&event);
    } else {
      yaml_event_delete(&event);
    }
  }
  return root;
}

void YAMLParser::parseMapping(const std::filesystem::path& filename,
    YAMLNode& node) {
  yaml_event_delete(&event);
  node.setMapping();
  bool done = false;
  while (not done) {
    /* read one name/value pair on each iteration */
    if (!yaml_parser_parse(&parser, &event)) {
      throw std::runtime_error("YAML syntax error in file " +
          filename.string());
    }
    if (event.type == YAML_SCALAR_EVENT) {
      /* key */
      auto data = reinterpret_cast<char*>(event.data.scalar.value);
      auto length = event.data.scalar.length;
      auto key = std::string{data, length};
      yaml_event_delete(&event);

      /* value */
      if (!yaml_parser_parse(&parser, &event)) {
        throw std::runtime_error("YAML syntax error in file " +
            filename.string());
      }
      if (event.type == YAML_SCALAR_EVENT) {
        parseValue(filename, node.insert(std::move(key)));
      } else if (event.type == YAML_SEQUENCE_START_EVENT) {
        parseSequence(filename, node.insert(std::move(key)));
      } else if (event.type == YAML_MAPPING_START_EVENT) {
        parseMapping(filename, node.insert(std::move(key)));
      } else {
        yaml_event_delete(&event);
      }
    } else {
      done = event.type == YAML_MAPPING_END_EVENT;
      yaml_event_delete(&event);
    }
  }
}

void YAMLParser::parseSequence(const std::filesystem::path& filename,
    YAMLNode& node) {
  yaml_event_delete(&event);
  node.setSequence();
  bool done = false;
  while (not done) {
    if (!yaml_parser_parse(&parser, &event)) {
      throw std::runtime_error("YAML syntax error in file " +
          filename.string());
    }
    if (event.type == YAML_SCALAR_EVENT) {
      parseValue(filename, node.push());
    } else if (event.type == YAML_SEQUENCE_START_EVENT) {
      parseSequence(filename, node.push());
    } else if (event.type == YAML_MAPPING_START_EVENT) {
      parseMapping(filename, node.push());
    } else {
      done = event.type == YAML_SEQUENCE_END_EVENT;
      yaml_event_delete(&event);
    }
  }
}

void YAMLParser::parseValue(const std::filesystem::path&,
    YAMLNode& node) {
  auto data = (char*)event.data.scalar.value;
  auto length = event.data.scalar.length;
  node.set(std::string(data, length));
  yaml_event_delete(&event);
}
