#ifndef IO_DESCRIPTOR_H
#define IO_DESCRIPTOR_H

#include <boost/any.hpp>
#include <string>
#include <vector>

/**
 * Temoto Tasking Protocol
 */
namespace TTP
{

/**
 * @brief The Data struct
 */
struct Data
{
    std::string type = "";
    boost::any value;
};

std::ostream& operator<<( std::ostream& stream, const Data& data);

// Valid datatypes
const std::vector<std::string> valid_datatypes = {"topic",
                                                  "number",
                                                  "pointer",
                                                  "other",
                                                  "string"};

// Valid subjects
const std::vector<std::string> valid_subjects = {"what",
                                                 "where"};

/**
 * @brief The Subject struct
 */
class Subject
{
public:
    std::string type_;
    std::string pos_tag_;
    std::vector<std::string> words_;
    std::vector<Data> data_;
    bool is_complete_ = true;

    Subject() = default;

    Subject(std::string type, std::string word);

    void markIncomplete();
    void markComplete();
};

std::ostream& operator<<( std::ostream& stream, const Subject& subject);

std::ostream& operator<<( std::ostream& stream, const std::vector<Subject>& subjects);
}
#endif
