#include "TTP/io_descriptor.h"
#include <iostream>

namespace TTP
{

Subject::Subject(std::string type, std::string word)
    : type_(type)
{
    words_.push_back(word);
}

void Subject::markIncomplete()
{
    is_complete_ = false;
}

void Subject::markComplete()
{
    is_complete_ = true;
}

// Get subjects by type
Subject getSubjectByType(const std::string& type, Subjects& subjects)
{
    for (auto sub_it = subjects.begin(); sub_it != subjects.end(); ++sub_it)
    {
        if(sub_it->type_ == type)
        {
            Subject ret_subject = *sub_it;
            subjects.erase(sub_it);

            return std::move(ret_subject);
        }
    }
    // TODO: throw temoto error
    throw;
}

// Subjects comparison operator
bool operator==(const std::vector<Subject>& subs_1, const std::vector<Subject>& subs_2)
{
    // create a copy of subs_2
    std::vector<Subject> subs_2_c = subs_2;
    bool all_match = false;

    for (auto& sub_1 : subs_1)
    {
        bool subject_match = false;
        for (unsigned int i=0; i<subs_2_c.size(); i++)
        {
            // Check for type match
            if (sub_1.type_ != subs_2_c[i].type_)
            {
                //std::cout << "    subject types do not match\n";
                continue;
            }

            // Check data size
            if (sub_1.data_.size() != subs_2_c[i].data_.size())
            {
                //std::cout << "    data size does not match\n";
                continue;
            }

            // Check data
            if (sub_1.data_ != subs_2_c[i].data_)
            {
                //std::cout << "    data does not match\n";
                continue;
            }

            //std::cout << "    we got a winner\n";
            subject_match = true;
            subs_2_c.erase(subs_2_c.begin() + i);
            break;
        }
        if (subject_match != true)
        {
            return false;
        }
    }
    return true;
}

// Subjects streaming operator
std::ostream& operator<<( std::ostream& stream, const std::vector<Subject>& subjects)
{
    // Print out the whats
    if (!subjects.empty())
    {
        for (const auto& subject : subjects)
        {
            stream << "| |_ " << subject;
        }
        stream << "|" << std::endl;
    }

    return stream;
}

// Subject streaming operator
std::ostream& operator<<( std::ostream& stream, const Subject& subject)
{
    // Print out the type
    stream << subject.type_ << ": ";

    // Print out if the subject is complete or not
    if (subject.is_complete_)
    {
        stream << " Complete";
    }
    else
    {
        stream << " Incomplete";
    }

    stream << " [";

    // Print out the word (candidate words)
    for (auto& word : subject.words_)
    {
        stream << word;
        if (&word != &subject.words_.back())
        {
            stream << ", ";
        }
    }

    stream << "]";

    // Print out the data
    if (!subject.data_.empty())
    {
        stream << " + data {";
        for (auto& data : subject.data_)
        {
            stream << data;
            if (&data != &subject.data_.back())
            {
                stream << ", ";
            }
        }
        stream << "}";
    }

    stream << std::endl;

    return stream;
}

// Data comparison operator
bool operator==(const Data& d1, const Data& d2)
{
    return (d1.type == d2.type);
}

// Data vector comparison operator
bool operator==(const std::vector<Data>& dv1, const std::vector<Data>& dv2)
{
    // Create a copy of dv2
    std::vector<Data> dv2_c = dv2;

    for (auto& d1 : dv1)
    {
        bool d_match = false;
        for (unsigned int i=0; i<dv2.size(); i++)
        {
            if (d1 == dv2_c[i])
            {
                d_match = true;
                dv2_c.erase(dv2_c.begin() + i);
                break;
            }
        }
        if (d_match != true)
        {
            return false;
        }
    }
    return true;
}

// Data streaming operator
std::ostream& operator<<( std::ostream& stream, const Data& data)
{
    stream << "[" << data.type;
    if (!data.value.empty())
    {
        try
        {
            if (data.type == "string")
            {
                stream << " : " << boost::any_cast<std::string>(data.value);
            }

            if (data.type == "topic")
            {
                stream << " : " << boost::any_cast<std::string>(data.value);
            }

            if (data.type == "number")
            {
                stream << " : " << boost::any_cast<double>(data.value);
            }
        }
        catch (boost::bad_any_cast& e)
        {
            stream << " : " << e.what();
        }
    }
    stream << "]";

    return stream;
}

}