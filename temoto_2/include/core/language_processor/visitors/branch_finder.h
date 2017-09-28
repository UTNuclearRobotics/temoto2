/**
 * @file branch_finder.h
 * @author Robert valner
 *
 * All files in META are released under the MIT license. For more details,
 * consult the file LICENSE in the root of the project.
 */

#ifndef META_PARSER_BRANCH_FINDER_H_
#define META_PARSER_BRANCH_FINDER_H_

#include <memory>
#include <vector>

#include "meta/parser/trees/visitors/visitor.h"
#include "meta/parser/sr_parser.h"

namespace meta
{
namespace parser
{

class Branch
{
public:

    std::vector<parser::parse_tree> verb_phrases_;

    std::vector<parser::parse_tree> noun_phrases_;

    std::vector<parser::parse_tree> prep_phrases_;

};

/**
 * This is a visitor that finds and extracts a branch in a parse tree.
 */
class branch_finder : public const_visitor<void>
{
  public:

    void operator()(const leaf_node&) override;
    void operator()(const internal_node&) override;

    /**
     * @brief Returns the phrases found by visitor
     * @return
     */
    std::vector<Branch> getBranches();

  private:
    /// The storage for the parse trees found so far
    std::vector<Branch> branches_;

};


}
}

#endif
