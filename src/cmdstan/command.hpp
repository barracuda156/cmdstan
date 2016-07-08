#ifndef CMDSTAN_COMMAND_HPP
#define CMDSTAN_COMMAND_HPP

#include <stan/old_services/arguments/argument_parser.hpp>
#include <stan/old_services/arguments/arg_data.hpp>
#include <stan/old_services/arguments/arg_id.hpp>
#include <stan/old_services/arguments/arg_init.hpp>
#include <stan/old_services/arguments/arg_output.hpp>
#include <stan/old_services/arguments/arg_random.hpp>
#include <stan/old_services/io/write_model.hpp>
#include <stan/old_services/io/write_stan.hpp>
#include <stan/interface_callbacks/interrupt/noop.hpp>
#include <stan/interface_callbacks/writer/base_writer.hpp>
#include <stan/interface_callbacks/writer/noop_writer.hpp>
#include <stan/interface_callbacks/writer/stream_writer.hpp>
#include <stan/io/dump.hpp>
#include <stan/services/diagnose/diagnose.hpp>
#include <stan/services/optimize/bfgs.hpp>
#include <stan/services/optimize/lbfgs.hpp>
#include <stan/services/optimize/newton.hpp>
#include <stan/services/sample/fixed_param.hpp>
#include <stan/services/sample/hmc_nuts_dense_e.hpp>
#include <stan/services/sample/hmc_nuts_dense_e_adapt.hpp>
#include <stan/services/sample/hmc_nuts_diag_e.hpp>
#include <stan/services/sample/hmc_nuts_diag_e_adapt.hpp>
#include <stan/services/sample/hmc_nuts_unit_e.hpp>
#include <stan/services/sample/hmc_nuts_unit_e_adapt.hpp>
#include <stan/services/sample/hmc_static_dense_e.hpp>
#include <stan/services/sample/hmc_static_dense_e_adapt.hpp>
#include <stan/services/sample/hmc_static_diag_e.hpp>
#include <stan/services/sample/hmc_static_diag_e_adapt.hpp>
#include <stan/services/sample/hmc_static_unit_e.hpp>
#include <stan/services/sample/hmc_static_unit_e_adapt.hpp>
#include <stan/services/experimental/advi/fullrank.hpp>
#include <stan/services/experimental/advi/meanfield.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <fstream>
#include <string>
#include <vector>

namespace stan {
  namespace services {

    stan::io::dump get_var_context(const std::string file) {
      std::fstream stream(file.c_str(), std::fstream::in);
      stan::io::dump var_context(stream);
      stream.close();
      return var_context;
    }

    template <class Model>
    int command(int argc, const char* argv[]) {
      stan::interface_callbacks::writer::stream_writer info(std::cout);
      stan::interface_callbacks::writer::stream_writer err(std::cout);


      // Read arguments
      std::vector<stan::services::argument*> valid_arguments;
      valid_arguments.push_back(new stan::services::arg_id());
      valid_arguments.push_back(new stan::services::arg_data());
      valid_arguments.push_back(new stan::services::arg_init());
      valid_arguments.push_back(new stan::services::arg_random());
      valid_arguments.push_back(new stan::services::arg_output());
      stan::services::argument_parser parser(valid_arguments);
      int err_code = parser.parse_args(argc, argv, info, err);
      if (err_code != 0) {
        std::cout << "Failed to parse arguments, terminating Stan" << std::endl;
        return err_code;
      }
      if (parser.help_printed())
        return err_code;
      stan::services::u_int_argument* random_arg = dynamic_cast<stan::services::u_int_argument*>(parser.arg("random")->arg("seed"));
      if (random_arg->is_default()) {
        random_arg->set_value((boost::posix_time::microsec_clock::universal_time() - boost::posix_time::ptime(boost::posix_time::min_date_time)).total_milliseconds());
      }
      parser.print(info);
      info();


      stan::interface_callbacks::writer::noop_writer init_writer;
      interface_callbacks::interrupt::noop interrupt;

      stan::io::dump data_var_context(get_var_context(dynamic_cast<stan::services::string_argument*>(parser.arg("data")->arg("file"))->value()));

      std::fstream output_stream(dynamic_cast<stan::services::string_argument*>(parser.arg("output")->arg("file"))->value(),
                                 std::fstream::out);
      stan::interface_callbacks::writer::stream_writer sample_writer(output_stream, "# ");

      std::fstream diagnostic_stream(dynamic_cast<stan::services::string_argument*>(parser.arg("output")->arg("diagnostic_file"))->value(),
                                     std::fstream::out);
      stan::interface_callbacks::writer::stream_writer diagnostic_writer(diagnostic_stream, "# ");


      //////////////////////////////////////////////////
      //                Initialize Model              //
      //////////////////////////////////////////////////
      Model model(data_var_context, &std::cout);
      io::write_stan(sample_writer);
      io::write_model(sample_writer, model.model_name());
      parser.print(sample_writer);

      io::write_stan(diagnostic_writer);
      io::write_model(diagnostic_writer, model.model_name());
      parser.print(diagnostic_writer);


      int refresh = dynamic_cast<stan::services::int_argument*>(parser.arg("output")->arg("refresh"))->value();
      unsigned int id = dynamic_cast<stan::services::int_argument*>(parser.arg("id"))->value();
      unsigned int random_seed = dynamic_cast<stan::services::u_int_argument*>(parser.arg("random")->arg("seed"))->value();

      std::string init = dynamic_cast<stan::services::string_argument*>(parser.arg("init"))->value();
      double init_radius = 2.0;
      try {
        init_radius = boost::lexical_cast<double>(init);
        init = "";
      } catch (const boost::bad_lexical_cast& e) {
      }
      stan::io::dump init_context(get_var_context(init));


      int return_code = stan::services::error_codes::CONFIG;
      if (parser.arg("method")->arg("diagnose")) {
        stan::services::list_argument* test = dynamic_cast<stan::services::list_argument*>(parser.arg("method")->arg("diagnose")->arg("test"));

        if (test->value() == "gradient") {
          double epsilon = dynamic_cast<stan::services::real_argument*>(test->arg("gradient")->arg("epsilon"))->value();
          double error = dynamic_cast<stan::services::real_argument*>(test->arg("gradient")->arg("error"))->value();
          return_code = stan::services::diagnose::diagnose(model,
                                                           init_context,
                                                           random_seed, id,
                                                           init_radius,
                                                           epsilon, error,
                                                           info,
                                                           init_writer,
                                                           sample_writer);
        }
      } else if (parser.arg("method")->arg("optimize")) {
        stan::services::list_argument* algo = dynamic_cast<stan::services::list_argument*>(parser.arg("method")->arg("optimize")->arg("algorithm"));
        int num_iterations = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("optimize")->arg("iter"))->value();
        bool save_iterations = dynamic_cast<stan::services::bool_argument*>(parser.arg("method")->arg("optimize")->arg("save_iterations"))->value();

        if (algo->value() == "newton") {
          return_code = stan::services::optimize::newton(model,
                                                         init_context,
                                                         random_seed,
                                                         id,
                                                         init_radius,
                                                         num_iterations,
                                                         save_iterations,
                                                         interrupt,
                                                         info,
                                                         init_writer,
                                                         sample_writer);
        } else if (algo->value() == "bfgs") {
          double init_alpha = dynamic_cast<stan::services::real_argument*>(algo->arg("bfgs")->arg("init_alpha"))->value();
          double tol_obj = dynamic_cast<services::real_argument*>(algo->arg("bfgs")->arg("tol_obj"))->value();
          double tol_rel_obj = dynamic_cast<stan::services::real_argument*>(algo->arg("bfgs")->arg("tol_rel_obj"))->value();
          double tol_grad = dynamic_cast<stan::services::real_argument*>(algo->arg("bfgs")->arg("tol_grad"))->value();
          double tol_rel_grad = dynamic_cast<stan::services::real_argument*>(algo->arg("bfgs")->arg("tol_rel_grad"))->value();
          double tol_param = dynamic_cast<stan::services::real_argument*>(algo->arg("bfgs")->arg("tol_param"))->value();

          return_code = stan::services::optimize::bfgs(model,
                                                       init_context,
                                                       random_seed,
                                                       id,
                                                       init_radius,
                                                       init_alpha,
                                                       tol_obj,
                                                       tol_rel_obj,
                                                       tol_grad,
                                                       tol_rel_grad,
                                                       tol_param,
                                                       num_iterations,
                                                       save_iterations,
                                                       refresh,
                                                       interrupt,
                                                       info,
                                                       init_writer,
                                                       sample_writer);
        } else if (algo->value() == "lbfgs") {
          int history_size = dynamic_cast<services::int_argument*>(algo->arg("lbfgs")->arg("history_size"))->value();
          double init_alpha = dynamic_cast<services::real_argument*>(algo->arg("lbfgs")->arg("init_alpha"))->value();
          double tol_obj = dynamic_cast<services::real_argument*>(algo->arg("lbfgs")->arg("tol_obj"))->value();
          double tol_rel_obj = dynamic_cast<services::real_argument*>(algo->arg("lbfgs")->arg("tol_rel_obj"))->value();
          double tol_grad = dynamic_cast<services::real_argument*>(algo->arg("lbfgs")->arg("tol_grad"))->value();
          double tol_rel_grad = dynamic_cast<services::real_argument*>(algo->arg("lbfgs")->arg("tol_rel_grad"))->value();
          double tol_param = dynamic_cast<services::real_argument*>(algo->arg("lbfgs")->arg("tol_param"))->value();

          return_code = stan::services::optimize::lbfgs(model,
                                                        init_context,
                                                        random_seed,
                                                        id,
                                                        init_radius,
                                                        history_size,
                                                        init_alpha,
                                                        tol_obj,
                                                        tol_rel_obj,
                                                        tol_grad,
                                                        tol_rel_grad,
                                                        tol_param,
                                                        num_iterations,
                                                        save_iterations,
                                                        refresh,
                                                        interrupt,
                                                        info,
                                                        init_writer,
                                                        sample_writer);
        }
      } else if (parser.arg("method")->arg("sample")) {
        int num_warmup = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("sample")->arg("num_warmup"))->value();
        int num_samples = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("sample")->arg("num_samples"))->value();
        int num_thin = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("sample")->arg("thin"))->value();
        bool save_warmup = dynamic_cast<stan::services::bool_argument*>(parser.arg("method")->arg("sample")->arg("save_warmup"))->value();
        stan::services::list_argument* algo = dynamic_cast<stan::services::list_argument*>(parser.arg("method")->arg("sample")->arg("algorithm"));
        stan::services::categorical_argument* adapt = dynamic_cast<stan::services::categorical_argument*>(parser.arg("method")->arg("sample")->arg("adapt"));
        bool adapt_engaged = dynamic_cast<stan::services::bool_argument*>(adapt->arg("engaged"))->value();

        if (model.num_params_r() == 0 && algo->value() != "fixed_param") {
          info("Must use algorithm=fixed_param for model that has no parameters.");
          return_code = stan::services::error_codes::CONFIG;
        } else if (algo->value() == "fixed_param") {
          return_code = stan::services::sample::fixed_param(model,
                                                            init_context,
                                                            random_seed,
                                                            id,
                                                            init_radius,
                                                            num_samples,
                                                            num_thin,
                                                            refresh,
                                                            interrupt,
                                                            info,
                                                            err,
                                                            init_writer,
                                                            sample_writer,
                                                            diagnostic_writer);
        } else if (algo->value() == "hmc") {
          stan::services::list_argument* engine = dynamic_cast<stan::services::list_argument*>(algo->arg("hmc")->arg("engine"));
          stan::services::list_argument* metric = dynamic_cast<stan::services::list_argument*>(algo->arg("hmc")->arg("metric"));
          stan::services::categorical_argument* adapt = dynamic_cast<stan::services::categorical_argument*>(parser.arg("method")->arg("sample")->arg("adapt"));

          stan::services::categorical_argument* hmc = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc"));
          double stepsize = dynamic_cast<stan::services::real_argument*>(hmc->arg("stepsize"))->value();
          double stepsize_jitter= dynamic_cast<stan::services::real_argument*>(hmc->arg("stepsize_jitter"))->value();

          if (engine->value() == "nuts" && metric->value() == "dense_e" && adapt_engaged == false) {
            int max_depth = dynamic_cast<stan::services::int_argument*>(dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("nuts"))->arg("max_depth"))->value();
            return_code = stan::services::sample::hmc_nuts_dense_e(model,
                                                                   init_context,
                                                                   random_seed,
                                                                   id,
                                                                   init_radius,
                                                                   num_warmup,
                                                                   num_samples,
                                                                   num_thin,
                                                                   save_warmup,
                                                                   refresh,
                                                                   stepsize,
                                                                   stepsize_jitter,
                                                                   max_depth,
                                                                   interrupt,
                                                                   info,
                                                                   err,
                                                                   init_writer,
                                                                   sample_writer,
                                                                   diagnostic_writer);
          } else if (engine->value() == "nuts" && metric->value() == "dense_e" && adapt_engaged == true) {
            int max_depth = dynamic_cast<stan::services::int_argument*>(dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("nuts"))->arg("max_depth"))->value();
            double delta = dynamic_cast<real_argument*>(adapt->arg("delta"))->value();
            double gamma = dynamic_cast<real_argument*>(adapt->arg("gamma"))->value();
            double kappa = dynamic_cast<real_argument*>(adapt->arg("kappa"))->value();
            double t0 = dynamic_cast<real_argument*>(adapt->arg("t0"))->value();
            unsigned int init_buffer = dynamic_cast<u_int_argument*>(adapt->arg("init_buffer"))->value();
            unsigned int term_buffer = dynamic_cast<u_int_argument*>(adapt->arg("term_buffer"))->value();
            unsigned int window = dynamic_cast<u_int_argument*>(adapt->arg("window"))->value();
            return_code = stan::services::sample::hmc_nuts_dense_e_adapt(model,
                                                                         init_context,
                                                                         random_seed,
                                                                         id,
                                                                         init_radius,
                                                                         num_warmup,
                                                                         num_samples,
                                                                         num_thin,
                                                                         save_warmup,
                                                                         refresh,
                                                                         stepsize,
                                                                         stepsize_jitter,
                                                                         max_depth,
                                                                         delta,
                                                                         gamma,
                                                                         kappa,
                                                                         t0,
                                                                         init_buffer,
                                                                         term_buffer,
                                                                         window,
                                                                         interrupt,
                                                                         info,
                                                                         err,
                                                                         init_writer,
                                                                         sample_writer,
                                                                         diagnostic_writer);
          } else if (engine->value() == "nuts" && metric->value() == "diag_e" && adapt_engaged == false) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("nuts"));
            int max_depth = dynamic_cast<stan::services::int_argument*>(base->arg("max_depth"))->value();
            return_code = stan::services::sample::hmc_nuts_diag_e(model,
                                                                  init_context,
                                                                  random_seed,
                                                                  id,
                                                                  init_radius,
                                                                  num_warmup,
                                                                  num_samples,
                                                                  num_thin,
                                                                  save_warmup,
                                                                  refresh,
                                                                  stepsize,
                                                                  stepsize_jitter,
                                                                  max_depth,
                                                                  interrupt,
                                                                  info,
                                                                  err,
                                                                  init_writer,
                                                                  sample_writer,
                                                                  diagnostic_writer);
          } else if (engine->value() == "nuts" && metric->value() == "diag_e" && adapt_engaged == true) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("nuts"));
            int max_depth = dynamic_cast<stan::services::int_argument*>(base->arg("max_depth"))->value();
            double delta = dynamic_cast<real_argument*>(adapt->arg("delta"))->value();
            double gamma = dynamic_cast<real_argument*>(adapt->arg("gamma"))->value();
            double kappa = dynamic_cast<real_argument*>(adapt->arg("kappa"))->value();
            double t0 = dynamic_cast<real_argument*>(adapt->arg("t0"))->value();
            unsigned int init_buffer = dynamic_cast<u_int_argument*>(adapt->arg("init_buffer"))->value();
            unsigned int term_buffer = dynamic_cast<u_int_argument*>(adapt->arg("term_buffer"))->value();
            unsigned int window = dynamic_cast<u_int_argument*>(adapt->arg("window"))->value();
            return_code = stan::services::sample::hmc_nuts_diag_e_adapt(model,
                                                                        init_context,
                                                                        random_seed,
                                                                        id,
                                                                        init_radius,
                                                                        num_warmup,
                                                                        num_samples,
                                                                        num_thin,
                                                                        save_warmup,
                                                                        refresh,
                                                                        stepsize,
                                                                        stepsize_jitter,
                                                                        max_depth,
                                                                        delta,
                                                                        gamma,
                                                                        kappa,
                                                                        t0,
                                                                        init_buffer,
                                                                        term_buffer,
                                                                        window,
                                                                        interrupt,
                                                                        info,
                                                                        err,
                                                                        init_writer,
                                                                        sample_writer,
                                                                        diagnostic_writer);
          } else if (engine->value() == "nuts" && metric->value() == "unit_e" && adapt_engaged == false) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("nuts"));
            int max_depth = dynamic_cast<stan::services::int_argument*>(base->arg("max_depth"))->value();
            return_code = stan::services::sample::hmc_nuts_unit_e(model,
                                                                  init_context,
                                                                  random_seed,
                                                                  id,
                                                                  init_radius,
                                                                  num_warmup,
                                                                  num_samples,
                                                                  num_thin,
                                                                  save_warmup,
                                                                  refresh,
                                                                  stepsize,
                                                                  stepsize_jitter,
                                                                  max_depth,
                                                                  interrupt,
                                                                  info,
                                                                  err,
                                                                  init_writer,
                                                                  sample_writer,
                                                                  diagnostic_writer);
          } else if (engine->value() == "nuts" && metric->value() == "unit_e" && adapt_engaged == true) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("nuts"));
            int max_depth = dynamic_cast<stan::services::int_argument*>(base->arg("max_depth"))->value();
            double delta = dynamic_cast<real_argument*>(adapt->arg("delta"))->value();
            double gamma = dynamic_cast<real_argument*>(adapt->arg("gamma"))->value();
            double kappa = dynamic_cast<real_argument*>(adapt->arg("kappa"))->value();
            double t0 = dynamic_cast<real_argument*>(adapt->arg("t0"))->value();
            return_code = stan::services::sample::hmc_nuts_unit_e_adapt(model,
                                                                        init_context,
                                                                        random_seed,
                                                                        id,
                                                                        init_radius,
                                                                        num_warmup,
                                                                        num_samples,
                                                                        num_thin,
                                                                        save_warmup,
                                                                        refresh,
                                                                        stepsize,
                                                                        stepsize_jitter,
                                                                        max_depth,
                                                                        delta,
                                                                        gamma,
                                                                        kappa,
                                                                        t0,
                                                                        interrupt,
                                                                        info,
                                                                        err,
                                                                        init_writer,
                                                                        sample_writer,
                                                                        diagnostic_writer);
          } else if (engine->value() == "static" && metric->value() == "dense_e" && adapt_engaged == false) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("static"));
            double int_time = dynamic_cast<stan::services::real_argument*>(base->arg("int_time"))->value();
            return_code = stan::services::sample::hmc_static_dense_e(model,
                                                                     init_context,
                                                                     random_seed,
                                                                     id,
                                                                     init_radius,
                                                                     num_warmup,
                                                                     num_samples,
                                                                     num_thin,
                                                                     save_warmup,
                                                                     refresh,
                                                                     stepsize,
                                                                     stepsize_jitter,
                                                                     int_time,
                                                                     interrupt,
                                                                     info,
                                                                     err,
                                                                     init_writer,
                                                                     sample_writer,
                                                                     diagnostic_writer);
          } else if (engine->value() == "static" && metric->value() == "dense_e" && adapt_engaged == true) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("static"));
            double int_time = dynamic_cast<stan::services::real_argument*>(base->arg("int_time"))->value();
            double delta = dynamic_cast<real_argument*>(adapt->arg("delta"))->value();
            double gamma = dynamic_cast<real_argument*>(adapt->arg("gamma"))->value();
            double kappa = dynamic_cast<real_argument*>(adapt->arg("kappa"))->value();
            double t0 = dynamic_cast<real_argument*>(adapt->arg("t0"))->value();
            unsigned int init_buffer = dynamic_cast<u_int_argument*>(adapt->arg("init_buffer"))->value();
            unsigned int term_buffer = dynamic_cast<u_int_argument*>(adapt->arg("term_buffer"))->value();
            unsigned int window = dynamic_cast<u_int_argument*>(adapt->arg("window"))->value();
            return_code = stan::services::sample::hmc_static_dense_e_adapt(model,
                                                                           init_context,
                                                                           random_seed,
                                                                           id,
                                                                           init_radius,
                                                                           num_warmup,
                                                                           num_samples,
                                                                           num_thin,
                                                                           save_warmup,
                                                                           refresh,
                                                                           stepsize,
                                                                           stepsize_jitter,
                                                                           int_time,
                                                                           delta,
                                                                           gamma,
                                                                           kappa,
                                                                           t0,
                                                                           init_buffer,
                                                                           term_buffer,
                                                                           window,
                                                                           interrupt,
                                                                           info,
                                                                           err,
                                                                           init_writer,
                                                                           sample_writer,
                                                                           diagnostic_writer);
          } else if (engine->value() == "static" && metric->value() == "diag_e" && adapt_engaged == false) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("static"));
            double int_time = dynamic_cast<stan::services::real_argument*>(base->arg("int_time"))->value();
            return_code = stan::services::sample::hmc_static_diag_e(model,
                                                                    init_context,
                                                                    random_seed,
                                                                    id,
                                                                    init_radius,
                                                                    num_warmup,
                                                                    num_samples,
                                                                    num_thin,
                                                                    save_warmup,
                                                                    refresh,
                                                                    stepsize,
                                                                    stepsize_jitter,
                                                                    int_time,
                                                                    interrupt,
                                                                    info,
                                                                    err,
                                                                    init_writer,
                                                                    sample_writer,
                                                                    diagnostic_writer);
          } else if (engine->value() == "static" && metric->value() == "diag_e" && adapt_engaged == true) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("static"));
            double int_time = dynamic_cast<stan::services::real_argument*>(base->arg("int_time"))->value();
            double delta = dynamic_cast<real_argument*>(adapt->arg("delta"))->value();
            double gamma = dynamic_cast<real_argument*>(adapt->arg("gamma"))->value();
            double kappa = dynamic_cast<real_argument*>(adapt->arg("kappa"))->value();
            double t0 = dynamic_cast<real_argument*>(adapt->arg("t0"))->value();
            unsigned int init_buffer = dynamic_cast<u_int_argument*>(adapt->arg("init_buffer"))->value();
            unsigned int term_buffer = dynamic_cast<u_int_argument*>(adapt->arg("term_buffer"))->value();
            unsigned int window = dynamic_cast<u_int_argument*>(adapt->arg("window"))->value();
            return_code = stan::services::sample::hmc_static_diag_e_adapt(model,
                                                                          init_context,
                                                                          random_seed,
                                                                          id,
                                                                          init_radius,
                                                                          num_warmup,
                                                                          num_samples,
                                                                          num_thin,
                                                                          save_warmup,
                                                                          refresh,
                                                                          stepsize,
                                                                          stepsize_jitter,
                                                                          int_time,
                                                                          delta,
                                                                          gamma,
                                                                          kappa,
                                                                          t0,
                                                                          init_buffer,
                                                                          term_buffer,
                                                                          window,
                                                                          interrupt,
                                                                          info,
                                                                          err,
                                                                          init_writer,
                                                                          sample_writer,
                                                                          diagnostic_writer);
          } else if (engine->value() == "static" && metric->value() == "unit_e" && adapt_engaged == false) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("static"));
            double int_time = dynamic_cast<stan::services::real_argument*>(base->arg("int_time"))->value();
            return_code = stan::services::sample::hmc_static_unit_e(model,
                                                                    init_context,
                                                                    random_seed,
                                                                    id,
                                                                    init_radius,
                                                                    num_warmup,
                                                                    num_samples,
                                                                    num_thin,
                                                                    save_warmup,
                                                                    refresh,
                                                                    stepsize,
                                                                    stepsize_jitter,
                                                                    int_time,
                                                                    interrupt,
                                                                    info,
                                                                    err,
                                                                    init_writer,
                                                                    sample_writer,
                                                                    diagnostic_writer);
          } else if (engine->value() == "static" && metric->value() == "unit_e" && adapt_engaged == true) {
            stan::services::categorical_argument* base = dynamic_cast<stan::services::categorical_argument*>(algo->arg("hmc")->arg("engine")->arg("static"));
            double int_time = dynamic_cast<stan::services::real_argument*>(base->arg("int_time"))->value();
            double delta = dynamic_cast<real_argument*>(adapt->arg("delta"))->value();
            double gamma = dynamic_cast<real_argument*>(adapt->arg("gamma"))->value();
            double kappa = dynamic_cast<real_argument*>(adapt->arg("kappa"))->value();
            double t0 = dynamic_cast<real_argument*>(adapt->arg("t0"))->value();
            return_code = stan::services::sample::hmc_static_unit_e_adapt(model,
                                                                          init_context,
                                                                          random_seed,
                                                                          id,
                                                                          init_radius,
                                                                          num_warmup,
                                                                          num_samples,
                                                                          num_thin,
                                                                          save_warmup,
                                                                          refresh,
                                                                          stepsize,
                                                                          stepsize_jitter,
                                                                          int_time,
                                                                          delta,
                                                                          gamma,
                                                                          kappa,
                                                                          t0,
                                                                          interrupt,
                                                                          info,
                                                                          err,
                                                                          init_writer,
                                                                          sample_writer,
                                                                          diagnostic_writer);
          }
        }
      } else if (parser.arg("method")->arg("variational")) {
        stan::services::list_argument* algo = dynamic_cast<stan::services::list_argument*>(parser.arg("method")->arg("variational")->arg("algorithm"));
        int grad_samples = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("variational")->arg("grad_samples"))->value();
        int elbo_samples = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("variational")->arg("elbo_samples"))->value();
        int max_iterations = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("variational")->arg("iter"))->value();
        double tol_rel_obj = dynamic_cast<stan::services::real_argument*>(parser.arg("method")->arg("variational")->arg("tol_rel_obj"))->value();
        double eta = dynamic_cast<stan::services::real_argument*>(parser.arg("method")->arg("variational")->arg("eta"))->value();
        bool adapt_engaged = dynamic_cast<stan::services::bool_argument*>(parser.arg("method")->arg("variational")->arg("adapt")->arg("engaged"))->value();
        int adapt_iterations = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("variational")->arg("adapt")->arg("iter"))->value();
        int eval_elbo = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("variational")->arg("eval_elbo"))->value();
        int output_samples = dynamic_cast<stan::services::int_argument*>(parser.arg("method")->arg("variational")->arg("output_samples"))->value();

        if (algo->value() == "fullrank") {
          return_code = stan::services::experimental::advi::fullrank(model,
                                                                     init_context,
                                                                     random_seed,
                                                                     id,
                                                                     init_radius,
                                                                     grad_samples,
                                                                     elbo_samples,
                                                                     max_iterations,
                                                                     tol_rel_obj,
                                                                     eta,
                                                                     adapt_engaged,
                                                                     adapt_iterations,
                                                                     eval_elbo,
                                                                     output_samples,
                                                                     interrupt,
                                                                     info,
                                                                     init_writer,
                                                                     sample_writer,
                                                                     diagnostic_writer);
        } else if (algo->value() == "meanfield") {
          return_code = stan::services::experimental::advi::meanfield(model,
                                                                      init_context,
                                                                      random_seed,
                                                                      id,
                                                                      init_radius,
                                                                      grad_samples,
                                                                      elbo_samples,
                                                                      max_iterations,
                                                                      tol_rel_obj,
                                                                      eta,
                                                                      adapt_engaged,
                                                                      adapt_iterations,
                                                                      eval_elbo,
                                                                      output_samples,
                                                                      interrupt,
                                                                      info,
                                                                      init_writer,
                                                                      sample_writer,
                                                                      diagnostic_writer);
        }
      }

      output_stream.close();
      diagnostic_stream.close();
      for (size_t i = 0; i < valid_arguments.size(); ++i)
        delete valid_arguments.at(i);
      return return_code;
    }

  }
}
#endif
