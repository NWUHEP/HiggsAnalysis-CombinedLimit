#include "HiggsAnalysis/CombinedLimit/interface/ProfileLikelihood.h"
#include "RooRealVar.h"
#include "RooArgSet.h"
#include "RooStats/ProfileLikelihoodCalculator.h"
#include "RooStats/LikelihoodInterval.h"
#include "RooStats/HypoTestResult.h"
#include "HiggsAnalysis/CombinedLimit/interface/Combine.h"
#include "HiggsAnalysis/CombinedLimit/interface/CloseCoutSentry.h"


#include <Math/MinimizerOptions.h>

using namespace RooStats;

ProfileLikelihood::ProfileLikelihood() :
    LimitAlgo("Profile Likelihood specific options")
{
    options_.add_options()
        ("minimizerAlgo",      boost::program_options::value<std::string>(&minimizerAlgo_)->default_value("Minuit2"), "Choice of minimizer (Minuit vs Minuit2)")
        ("minimizerTolerance", boost::program_options::value<float>(&minimizerTolerance_)->default_value(1e-3),  "Tolerance for minimizer")
    ;
}

void ProfileLikelihood::applyOptions(const boost::program_options::variables_map &vm) 
{
}

ProfileLikelihood::MinimizerSentry::MinimizerSentry(std::string &minimizerAlgo, double tolerance) :
    minimizerTypeBackup(ROOT::Math::MinimizerOptions::DefaultMinimizerType()),
    minimizerAlgoBackup(ROOT::Math::MinimizerOptions::DefaultMinimizerAlgo()),
    minimizerTollBackup(ROOT::Math::MinimizerOptions::DefaultTolerance())
{
  ROOT::Math::MinimizerOptions::SetDefaultTolerance(tolerance);
  if (minimizerAlgo.find(",") != std::string::npos) {
      size_t idx = minimizerAlgo.find(",");
      std::string type = minimizerAlgo.substr(0,idx), algo = minimizerAlgo.substr(idx+1);
      if (verbose > 1) std::cout << "Set default minimizer to " << type << ", algorithm " << algo << std::endl;
      ROOT::Math::MinimizerOptions::SetDefaultMinimizer(type.c_str(), algo.c_str());
  } else {
      if (verbose > 1) std::cout << "Set default minimizer to " << minimizerAlgo << std::endl;
      ROOT::Math::MinimizerOptions::SetDefaultMinimizer(minimizerAlgo.c_str());
  }
}

ProfileLikelihood::MinimizerSentry::~MinimizerSentry() 
{
  ROOT::Math::MinimizerOptions::SetDefaultTolerance(minimizerTollBackup);
  ROOT::Math::MinimizerOptions::SetDefaultMinimizer(minimizerTypeBackup.c_str(),minimizerAlgoBackup.empty() ? 0 : minimizerAlgoBackup.c_str());
}

bool ProfileLikelihood::run(RooWorkspace *w, RooAbsData &data, double &limit, const double *hint) { 
  MinimizerSentry minimizerConfig(minimizerAlgo_, minimizerTolerance_);
  CloseCoutSentry sentry(verbose < 0);

  bool success = (doSignificance_ ?  runSignificance(w,data,limit) : runLimit(w,data,limit));
  return success;
}

bool ProfileLikelihood::runLimit(RooWorkspace *w, RooAbsData &data, double &limit) {
  RooRealVar *r = w->var("r");
  RooArgSet  poi(*r);
  double rMax = r->getMax();
  bool success = false;
  CloseCoutSentry coutSentry(verbose <= 1); // close standard output and error, so that we don't flood them with minuit messages

  while (!success) {
    ProfileLikelihoodCalculator plcB(data, *w->pdf("model_s"), poi);
    plcB.SetConfidenceLevel(cl);
    std::auto_ptr<LikelihoodInterval> plInterval(plcB.GetInterval());
    if (plInterval.get() == 0) break;
    limit = plInterval->UpperLimit(*r); 
    if (limit >= 0.75*r->getMax()) { 
      std::cout << "Limit r < " << limit << "; r max < " << r->getMax() << std::endl;
      if (r->getMax()/rMax > 20) break;
      r->setMax(r->getMax()*2); 
      continue;
    }
    if (limit == r->getMin()) {
      std::cerr << "ProfileLikelihoodCalculator failed (returned upper limit equal to the lower bound)" << std::endl;
      break;
    }
    success = true;
  }
  coutSentry.clear();
  if (verbose >= 0) {
      if (success) {
        std::cout << "\n -- Profile Likelihood -- " << "\n";
        std::cout << "Limit: r < " << limit << " @ " << cl * 100 << "% CL" << std::endl;
      }
  }
  return success;
}

bool ProfileLikelihood::runSignificance(RooWorkspace *w, RooAbsData &data, double &limit) {
  RooRealVar *r = w->var("r");
  RooArgSet  poi(*r);

  ProfileLikelihoodCalculator plcS(data, *w->pdf("model_s"), poi);

  RooArgSet nullParamValues; 
  nullParamValues.addClone(*r); ((RooRealVar&)nullParamValues["r"]).setVal(0);
  plcS.SetNullParameters(nullParamValues);

  CloseCoutSentry coutSentry(verbose <= 1); // close standard output and error, so that we don't flood them with minuit messages
  std::auto_ptr<HypoTestResult> result(plcS.GetHypoTest());
  if (result.get() == 0) return false;
  coutSentry.clear();

  limit = result->Significance();
  if (limit == 0 && signbit(limit)) {
      std::cerr << "ProfileLikelihoodCalculator failed (returned significance -0)" << std::endl;
      return false;
  }
  std::cout << "\n -- Profile Likelihood -- " << "\n";
  std::cout << "Significance: " << limit << std::endl;
  return true;
}
