#ifndef CMDSTAN_ARGUMENTS_ARG_SINGLE_REAL_POS_HPP
#define CMDSTAN_ARGUMENTS_ARG_SINGLE_REAL_POS_HPP

#include <cmdstan/arguments/singleton_argument.hpp>
#include <string>

/** Generic pos real value argument */

namespace cmdstan {

class arg_single_real_pos : public real_argument {
 public:
  arg_single_real_pos(const char* name, const char* desc, double def)
      : real_argument() {
    _name = name;
    _description = desc;
    _validity = std::string("0 < ").append(name);
    _default = std::to_string(def);
    _default_value = def;
    _value = _default_value;
  }

  bool is_valid(double value) { return value > 0; }
};

}  // namespace cmdstan
#endif
