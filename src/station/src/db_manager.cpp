#include <iostream>
#include <sstream>
#include <string>

#include "include/db_manager.hpp"
#include "include/packet.hpp"

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_array;
using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::basic::sub_array;

std::map<uint8_t, std::string> packet_name
    = { { (uint8_t)header_type::ACK, "ACK" },
        { (uint8_t)header_type::PING, "PING" },
        { (uint8_t)header_type::READ, "READ" },
        { (uint8_t)header_type::WRITE, "WRITE" },
        { (uint8_t)header_type::EXEC, "EXEC" },
        { (uint8_t)body_type::MEMORY, "MEMORY" },
        { (uint8_t)body_type::SENSORS, "SENSORS" },
        { (uint8_t)body_type::CODE, "CODE" } };

/// Default constructor
/// TODO: Read config values from file
DBManager::DBManager ()
{
  this->client_uri = mongocxx::uri ("mongodb://localhost:27017");
  this->client = mongocxx::client (client_uri);
  this->db = client["SRAM"];
}

/// Parametrized constructor
DBManager::DBManager (const std::string &uri, const std::string &db_name)
{
  this->client_uri = mongocxx::uri (uri);
  this->client = mongocxx::client (client_uri);
  this->db = client[db_name];
}

/// mongocxx::client does not provide a way to close the connection directly.

std::vector<uint8_t>
invert_bytes_arr (std::vector<uint8_t> &bytes)
{
  std::vector<uint8_t> inverted (bytes.size ());
  for (size_t b = 0; b < bytes.size (); ++b)
    {
      inverted[b] = bytes[b] ^ 0xFF;
    }
  return inverted;
}

/// Convert a header into a document
/// Every header has the same size
bson_doc
DBManager::header_to_doc (const header_t &header)
{
  auto doc = bson_doc{};

  std::string bid = fmt::format ("0x{0:08X}{1:08X}{2:08X}", header.bid_high,
                                 header.bid_medium, header.bid_low);

  auto date = bsoncxx::types::b_date (std::chrono::system_clock::now ());

  doc.append (kvp ("header_type", packet_name[header.type]));
  doc.append (kvp ("CRC", header.CRC));
  doc.append (kvp ("TTL", header.TTL));

  doc.append (kvp ("board_id", bid));
  doc.append (kvp ("creation_time", date));

  return doc;
}

/// Convert a body into a document
/// The data gets converted into a string with comma separated values
bson_doc
DBManager::body_to_doc (const body_t &body)
{
  auto doc = bson_doc{};
  auto data_arr = bsoncxx::builder::basic::array{};

  std::string bid = fmt::format ("0x{0:08X}{1:08X}{2:08X}", body.bid_high,
                                 body.bid_medium, body.bid_low);

  auto date = bsoncxx::types::b_date (std::chrono::system_clock::now ());

  doc.append (kvp ("body_type", packet_name[body.type]));
  doc.append (kvp ("CRC", body.CRC));
  doc.append (kvp ("board_id", bid));
  doc.append (kvp ("creation_time", date));
  doc.append (kvp ("mem_address", fmt::format ("0x{:08x}", body.mem_address)));

  std::stringstream data_ss;

  for (int byte = 0; byte < 511; ++byte)
    data_ss << byte << ",";
  data_ss << body.data[512];

  doc.append (kvp ("data", data_ss.str ()));

  return doc;
}

/// Insert one sample into the collection
MaybeResult
DBManager::insert_one (const bson_doc &doc, const std::string &coll_name)
{
  auto coll = this->db[coll_name];

  auto view = doc.view ();
  auto result = coll.insert_one (view);
  return result;
}

/// Returns true if the reference document exists in the db
/// The reference sample is the first sample that is taken from each board
/// Both board_id and mem_address are hex strings
bool
DBManager::reference_present (const std::string &board_id,
                              const std::string &mem_address)
{
  auto cursor = db["references"].find (make_document (
      kvp ("board_id", board_id), kvp ("mem_address", mem_address)));
  return std::distance (cursor.begin (), cursor.end ()) > 0;
}

/// Obtain the data from a sample in a vector
/// As the data is stored in a comma separated string to preserve space,
/// we need to convert it into a number vector
std::vector<uint8_t>
DBManager::get_data_vector (const std::string &board_id,
                            const std::string &mem_address)
{
  std::vector<uint8_t> values;

  auto cursor = this->db["references"].find (make_document (
      kvp ("board_id", board_id), kvp ("mem_address", mem_address)));

  for (auto &doc : cursor)
    {
      bsoncxx::document::element ele = doc["data"];
      std::string data_str = ele.get_utf8 ().value.to_string ();
      std::vector<std::string> split_bytes;

      std::stringstream ss (data_str);
      std::string item;
      while (std::getline (ss, item, ','))
        {
          values.push_back (std::stoi (item));
        }
    }
  return values;
}
