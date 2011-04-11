//
// File: PartNH.cpp
// Created by: Julien Dutheil
// Created on: Dec Thu 09 11:11 2010
//

/*
Copyright or © or Copr. CNRS

This software is a computer program whose purpose is to describe
the patterns of substitutions along a phylogeny using substitution mapping.

This software is governed by the CeCILL  license under French law and
abiding by the rules of distribution of free software.  You can  use, 
modify and/ or redistribute the software under the terms of the CeCILL
license as circulated by CEA, CNRS and INRIA at the following URL
"http://www.cecill.info". 

As a counterpart to the access to the source code and  rights to copy,
modify and redistribute granted by the license, users are provided only
with a limited warranty  and the software's author,  the holder of the
economic rights,  and the successive licensors  have only  limited
liability. 

In this respect, the user's attention is drawn to the risks associated
with loading,  using,  modifying and/or developing or reproducing the
software by the user in light of its specific status of free software,
that may mean  that it is complicated to manipulate,  and  that  also
therefore means  that it is reserved for developers  and  experienced
professionals having in-depth computer knowledge. Users are therefore
encouraged to load and test the software's suitability as regards their
requirements in conditions enabling the security of their systems and/or 
data to be ensured and,  more generally, to use and operate it in the 
same conditions as regards security. 

The fact that you are presently reading this means that you have had
knowledge of the CeCILL license and that you accept its terms.
*/

// From the STL:
#include <iostream>
#include <iomanip>

using namespace std;

// From bpp-phyl:
#include <Bpp/Phyl/Tree.h>
#include <Bpp/Phyl/App/PhylogeneticsApplicationTools.h>
#include <Bpp/Phyl/Io/Newick.h>
#include <Bpp/Phyl/Io/Nhx.h>
#include <Bpp/Phyl/Likelihood.all>
#include <Bpp/Phyl/OptimizationTools.h>

// From bpp-seq:
#include <Bpp/Seq/Alphabet.all>
#include <Bpp/Seq/Container/VectorSiteContainer.h>
#include <Bpp/Seq/SiteTools.h>
#include <Bpp/Seq/App/SequenceApplicationTools.h>

// From bpp-core:
#include <Bpp/App/BppApplication.h>
#include <Bpp/App/ApplicationTools.h>
#include <Bpp/Io/FileTools.h>
#include <Bpp/Text/TextTools.h>
#include <Bpp/Numeric/Prob/DiscreteDistribution.h>
#include <Bpp/Numeric/Prob/ConstantDistribution.h>

using namespace bpp;

/******************************************************************************/

void help()
{
  (*ApplicationTools::message << "__________________________________________________________________________").endLine();
  (*ApplicationTools::message << "partnh parameter1_name=parameter1_value parameter2_name=parameter2_value").endLine();
  (*ApplicationTools::message << "      ... param=option_file").endLine();
  (*ApplicationTools::message).endLine();
  (*ApplicationTools::message << "  Refer to the package manual for a list of available options.").endLine();
  (*ApplicationTools::message << "__________________________________________________________________________").endLine();
}

class Partition {
  public:
    unsigned int number;
    unsigned int size;
};

vector<const Node*> getCandidateNodesForThreshold(map<double, vector<const Node*> >& sortedHeights, double threshold) {
  vector<const Node*> candidates;
  for (map<double, vector<const Node*> >::iterator it = sortedHeights.begin(); it != sortedHeights.end(); ++it) {
    if (it->first <= threshold) {
      for (vector<const Node*>::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2)
        for (unsigned int k = 0; k < (*it2)->getNumberOfSons(); ++k)
          candidates.push_back((*it2)->getSon(k));
    }
  }
  return candidates;
}

vector< vector<int> > getGroups(vector<const Node*>& candidates) {
  map<int, Partition> partitions;
  for (size_t i = 0; i < candidates.size(); ++i) {
    vector<string> ids = TreeTemplateTools::getLeavesNames(*candidates[i]);
    unsigned int size = ids.size();
    for (size_t j = 0; j < ids.size(); ++j) {
      Partition* part = &partitions[TextTools::toInt(ids[j])];
      if (part->size == 0) { //new partition
        part->number = i;
        part->size = size;
      } else { //Partition already exists.
        //Check if this is a smaller partition:
        if (part->size > size) {
          part->number = i;
          part->size = size;
        }
      }
    }
  }
  //Now reorganize results:
  map<unsigned int, vector<int> > groups;
  for (map<int, Partition>::iterator it = partitions.begin(); it != partitions.end(); ++it) {
    groups[it->second.number].push_back(it->first);
  }
  //And renumber partitions:
  vector< vector<int> > groups2;
  for (map<unsigned int, vector<int> >::const_iterator it = groups.begin(); it != groups.end(); it++)
    groups2.push_back(it->second);

  //Return results
  return groups2;
}
 
SubstitutionModelSet* buildModelSetFromPartitions(
    const SubstitutionModel* model,
    const FrequenciesSet* rootFreqs,
    const Tree* tree,
    const vector< vector<int> >& groups,
    const vector<string>& globalParameterNames,
    std::map<int, ParameterList>& initParameters
  ) throw (AlphabetException, Exception)
{
  //Check alphabet:
  if (rootFreqs && model->getAlphabet()->getAlphabetType() != rootFreqs->getAlphabet()->getAlphabetType())
    throw AlphabetMismatchException("SubstitutionModelSetTools::createNonHomogeneousModelSet()", model->getAlphabet(), rootFreqs->getAlphabet());
  ParameterList globalParameters, branchParameters;
  globalParameters = model->getParameters();
  vector<string> globalParameterPrefs; // vector of the prefixes (when there is a '*' in the declaration)
  //First check if parameter names are valid:
  for ( size_t i = 0; i < globalParameterNames.size(); i++)
  {
    if (globalParameterNames[i].find("*") != string::npos) {
      size_t j = globalParameterNames[i].find("*");
      globalParameterPrefs.push_back(globalParameterNames[i].substr(0,j));
    }
    else if (!globalParameters.hasParameter(globalParameterNames[i]))
      throw Exception("SubstitutionModelSetTools::createNonHomogeneousModelSet. Parameter '" + globalParameterNames[i] + "' is not valid.");
  }
  
  for ( size_t i = globalParameters.size(); i > 0; i--)
  {
    string gN = globalParameters[i - 1].getName();
    bool flag = false;
    for ( size_t j = 0; j < globalParameterPrefs.size(); j++)
      if (gN.find(globalParameterPrefs[j]) == 0) {
        flag = true;
        break;
      }
    if (!flag)
      flag = (find(globalParameterNames.begin(), globalParameterNames.end(), globalParameters[i - 1].getName()) != globalParameterNames.end());

    if (!flag){
      //not a global parameter:
      branchParameters.addParameter(globalParameters[i - 1]);
      globalParameters.deleteParameter(i - 1);
    }
  }
  SubstitutionModelSet* modelSet = rootFreqs ?
    new SubstitutionModelSet(model->getAlphabet(), rootFreqs->clone()) :
    new SubstitutionModelSet(model->getAlphabet(), true);
  //We assign a copy of this model to all nodes in the tree, for each partition, and link all parameters with it.
  for (size_t i = 0; i < groups.size(); ++i) {
    SubstitutionModel* modelC = dynamic_cast<SubstitutionModel*>(model->clone());
    modelC->matchParametersValues(initParameters[groups[i][0]]);
    modelSet->addModel(modelC, groups[i], branchParameters.getParameterNames());
  }
  vector<int> allIds = tree->getNodesId();
  int rootId = tree->getRootId();
  unsigned int pos = 0;
  for (size_t i = 0; i < allIds.size(); i++) {
    if (allIds[i] == rootId) {
      pos = i;
      break;
    }
  }
  allIds.erase(allIds.begin() + pos);
  //Now add global parameters to all nodes:
  modelSet->addParameters(globalParameters, allIds);
  return modelSet;
}

ParameterList getParametersToEstimate(const DRTreeLikelihood* drtl, map<string, string>& params) {
  // Should I ignore some parameters?
  ParameterList parametersToEstimate = drtl->getParameters();
  string paramListDesc = ApplicationTools::getStringParameter("optimization.ignore_parameter", params, "", "", true, false);
  StringTokenizer st(paramListDesc, ",");
  while (st.hasMoreToken())
  {
    try
    {
      string param = st.nextToken();
      size_t starpos;
      if (param == "BrLen")
      {
        vector<string> vs = drtl->getBranchLengthsParameters().getParameterNames();
        parametersToEstimate.deleteParameters(vs);
        ApplicationTools::displayResult("Parameter ignored", string("Branch lengths"));
      }
      else if ((starpos = param.find("*")) != string::npos)
      {
        string pref = param.substr(0, starpos);
        vector<string> vs;
        for (unsigned int j = 0; j < parametersToEstimate.size(); j++)
        {
          if (parametersToEstimate[j].getName().find(pref) == 0)
            vs.push_back(parametersToEstimate[j].getName());
        }
        for (vector<string>::iterator it = vs.begin(); it != vs.end(); it++)
        {
          parametersToEstimate.deleteParameter(*it);
          ApplicationTools::displayResult("Parameter ignored", *it);
        }
      }
      else
      {
        parametersToEstimate.deleteParameter(param);
        ApplicationTools::displayResult("Parameter ignored", param);
      }
    }
    catch (ParameterNotFoundException& pnfe)
    {
      ApplicationTools::displayWarning("Parameter '" + pnfe.getParameter() + "' not found, and so can't be ignored!");
    }
  }
  return parametersToEstimate;
}

void estimateLikelihood(DRTreeLikelihood* drtl, ParameterList& parametersToEstimate, double tolerance, unsigned int nbEvalMax, OutputStream* messageHandler, OutputStream* profiler, bool reparam, unsigned int verbose) {
  // Uses Newton-raphson algorithm with numerical derivatives when required.
  parametersToEstimate.matchParametersValues(drtl->getParameters());
  OptimizationTools::optimizeNumericalParameters2(
    dynamic_cast<DiscreteRatesAcrossSitesTreeLikelihood*>(drtl), parametersToEstimate,
    0, tolerance, nbEvalMax, messageHandler, profiler, reparam, verbose, OptimizationTools::OPTIMIZATION_NEWTON);
}

int main(int args, char ** argv)
{
  cout << "******************************************************************" << endl;
  cout << "*                     PartNH, version 0.1.0                      *" << endl;
  cout << "* Authors: J. Dutheil                       Created on  09/12/10 *" << endl;
  cout << "*          B. Boussau                       Last Modif. 08/04/11 *" << endl;
  cout << "******************************************************************" << endl;
  cout << endl;

  if (args == 1)
  {
    help();
    exit(0);
  }
  
  try {

  BppApplication partnh(args, argv, "PartNH");
  partnh.startTimer();

  Newick newick;
  string clusterTree = ApplicationTools::getAFilePath("input.cluster_tree.file", partnh.getParams(), true, true);
  ApplicationTools::displayResult("Input cluster tree", clusterTree);
  TreeTemplate<Node>* htree = newick.read(clusterTree);
  
  //We only read NHX tree because we want to be sure to use the correct id:
  //TreeTemplate<Node>* ptree = dynamic_cast<TreeTemplate<Node>*>(PhylogeneticsApplicationTools::getTree(partnh.getParams()));
  string treeIdPath = ApplicationTools::getAFilePath("input.tree.file", partnh.getParams(), true, true);
  ApplicationTools::displayResult("Input tree file", treeIdPath);
  Nhx nhx(true);
  TreeTemplate<Node>* ptree = nhx.read(treeIdPath);

  map<const Node*, double> heights;
  TreeTemplateTools::getHeights(*htree->getRootNode(), heights);
  map<double, vector<const Node*> > sortedHeights;

  string method = ApplicationTools::getStringParameter("partition.method", partnh.getParams(), "threshold");
  for (map<const Node*, double>::iterator it = heights.begin(); it != heights.end(); ++it)
    sortedHeights[max(0., 1. - 2. * it->second)].push_back(it->first);

  //This will contains the final groups of nodes:
  vector< vector<int> > groups;

  if (method == "threshold") {
    double threshold = ApplicationTools::getDoubleParameter("partition.threshold", partnh.getParams(), 0.01);
    ApplicationTools::displayResult("Output partitions for threshold", threshold);
    vector<const Node*> candidates = getCandidateNodesForThreshold(sortedHeights, threshold);
    ApplicationTools::displayResult("Number of nested partitions", candidates.size());
   
    groups = getGroups(candidates);
    ApplicationTools::displayResult("Number of real partitions", groups.size());
    //Display partitions:
    for (size_t i = 0; i < groups.size(); ++i)
      ApplicationTools::displayResult("Partition " + TextTools::toString(i + 1), TextTools::toString(groups[i].size()) + " element(s).");
  
  } else if (method == "auto") {
    //First we need to get the alphabet and data:
    Alphabet* alphabet = SequenceApplicationTools::getAlphabet(partnh.getParams(), "", false);
    VectorSiteContainer* allSites = SequenceApplicationTools::getSiteContainer(alphabet, partnh.getParams());
    VectorSiteContainer* sites = SequenceApplicationTools::getSitesToAnalyse(*allSites, partnh.getParams());
    delete allSites;
    unsigned int nbSites = sites->getNumberOfSites();

    ApplicationTools::displayResult("Number of sequences", sites->getNumberOfSequences());
    ApplicationTools::displayResult("Number of sites", nbSites);
 
    //Then we need the model to be used, including substitution model, rate distribution and root frequencies set.
    //We also need to specify the parameters that will be shared by all partitions.
    SubstitutionModel* model = PhylogeneticsApplicationTools::getSubstitutionModel(alphabet, sites, partnh.getParams());
    DiscreteDistribution* rDist = 0;
    if (model->getName() != "RE08") SiteContainerTools::changeGapsToUnknownCharacters(*sites);
    if (model->getNumberOfStates() >= 2 * model->getAlphabet()->getSize())
    {
      // Markov-modulated Markov model!
      rDist = new ConstantDistribution(1., true);
    }
    else
    {
      rDist = PhylogeneticsApplicationTools::getRateDistribution(partnh.getParams());
    }
    //We initialize the procedure by estimating the homogeneous model.
    DRTreeLikelihood* drtl = 0;
    if (dynamic_cast<MixedSubstitutionModel*>(model) == 0)
      drtl = new DRHomogeneousTreeLikelihood(*ptree, *sites, model, rDist, true);
    else
      throw Exception("Mixed models not supported so far.");
      //drtl = new DRHomogeneousMixedTreeLikelihood(*ptree, *sites, model, rDist, true);
    drtl->initialize();
    
    //We get this now, so that it does not fail after having done the full optimization!!
    string modelPath = ApplicationTools::getAFilePath("output.model.file", partnh.getParams(), true, false);
    string logPath = ApplicationTools::getAFilePath("output.log.file", partnh.getParams(), false, false);
    ApplicationTools::displayResult("Output log to", logPath);
    auto_ptr<ofstream> logout;
    if (logPath != "none" && logPath != "None") {
      logout.reset(new ofstream(logPath.c_str(), ios::out));
      *logout << "Threshold\tNbPartitions\tLogL\tDF\tAIC\tBIC" << endl;
    }

    //Optimize parameters
    unsigned int optVerbose = ApplicationTools::getParameter<unsigned int>("optimization.verbose", partnh.getParams(), 2);
    
    string mhPath = ApplicationTools::getAFilePath("optimization.message_handler", partnh.getParams(), false, false);
    auto_ptr<OutputStream> messageHandler(
      (mhPath == "none") ? 0 :
      (mhPath == "std") ? ApplicationTools::message :
      new StlOutputStream(auto_ptr<ostream>(new ofstream(mhPath.c_str(), ios::out))));
    ApplicationTools::displayResult("Message handler", mhPath + "*");

    string prPath = ApplicationTools::getAFilePath("optimization.profiler", partnh.getParams(), false, false);
    auto_ptr<OutputStream> profiler(
      (prPath == "none") ? 0 :
      (prPath == "std") ? ApplicationTools::message :
      new StlOutputStream(auto_ptr<ostream>(new ofstream(prPath.c_str(), ios::out))));
    if (profiler.get()) profiler->setPrecision(20);
    ApplicationTools::displayResult("Profiler", prPath + "*");

    ParameterList parametersToEstimate = getParametersToEstimate(drtl, partnh.getParams()); 

    unsigned int nbEvalMax = ApplicationTools::getParameter<unsigned int>("optimization.max_number_f_eval", partnh.getParams(), 1000000);
    ApplicationTools::displayResult("Max # ML evaluations", TextTools::toString(nbEvalMax));

    double tolerance = ApplicationTools::getDoubleParameter("optimization.tolerance", partnh.getParams(), .000001);
    ApplicationTools::displayResult("Tolerance", TextTools::toString(tolerance));

    bool reparam = ApplicationTools::getBooleanParameter("optimization.reparametrization", partnh.getParams(), false);
    ApplicationTools::displayResult("Reparametrization", (reparam ? "yes" : "no"));
    
    estimateLikelihood(drtl, parametersToEstimate, tolerance, nbEvalMax, messageHandler.get(), profiler.get(), reparam, optVerbose);

    double logL = drtl->getValue();
    double df = static_cast<double>(drtl->getParameters().size());
    double aic = 2. * (df + logL);
    double bic = 2. * logL + df * log(nbSites);
    ApplicationTools::displayResult("* Homogeneous model - LogL", -logL);
    ApplicationTools::displayResult("                    - df", df);
    if (logout.get())
      *logout << 0. << "\t" << 1. << "\t" << -logL << "\t" << df << "\t" << aic << "\t" << bic << endl;
    
    //Get necessary things for building a non-homogeneous model:
    vector<double> rateFreqs;
    if (model->getNumberOfStates() != alphabet->getSize())
    {
      // Markov-Modulated Markov Model...
      unsigned int n = static_cast<unsigned int>(model->getNumberOfStates() / alphabet->getSize());
      rateFreqs = vector<double>(n, 1. / (double)n); // Equal rates assumed for now, may be changed later (actually, in the most general case,
                                                     // we should assume a rate distribution for the root also!!!
    }

    bool stationarity = ApplicationTools::getBooleanParameter("nonhomogeneous.stationarity", partnh.getParams(), false, "", false, false);
    FrequenciesSet* rootFreqs = 0;
    if (!stationarity)
    {
      rootFreqs = PhylogeneticsApplicationTools::getRootFrequenciesSet(alphabet, sites, partnh.getParams(), rateFreqs);
      stationarity = !rootFreqs;
    }
    ApplicationTools::displayBooleanResult("Stationarity assumed", stationarity);
    if (!stationarity && !ptree->isRooted())
      ApplicationTools::displayWarning("An (most likely) unrooted tree is in use with a non-stationary model! This is probably not what you want...");
   
    vector<string> globalParameters = ApplicationTools::getVectorParameter<string>("nonhomogeneous.shared_parameters", partnh.getParams(), ',', "");
    for (unsigned int i = 0; i < globalParameters.size(); i++)
      ApplicationTools::displayResult("Global parameter", globalParameters[i]);

    string likelihoodComparison = ApplicationTools::getStringParameter("partition.test", partnh.getParams(), "LRT");
    double testThreshold = 0.;
    if (likelihoodComparison == "LRT")
      testThreshold = ApplicationTools::getDoubleParameter("partition.test.threshold", partnh.getParams(), 0.01);
    else if (likelihoodComparison == "AIC") {}
    else if (likelihoodComparison == "BIC") {}
    else
      throw Exception("Unknown likelihood comparison method: " + likelihoodComparison);
    ApplicationTools::displayResult("AIC", aic);
    ApplicationTools::displayResult("BIC", bic);
    ApplicationTools::displayResult("Likelihood comparison method", likelihoodComparison);
      
    string stopCond = ApplicationTools::getStringParameter("partition.test.stop_condition", partnh.getParams(), "1");
    int stop;
    if (stopCond == "all") {
      stop = -log(0); //Inf
      ApplicationTools::displayResult("Stop condition", string("test all nested models and report global best"));
    } else {
      stop = TextTools::toInt(stopCond);
      if (stop < 1)
        throw Exception("Stop parameter should be at least 1!.");
      ApplicationTools::displayResult("Stop condition", string("test ") + TextTools::toString(stop) + string(" nested models after local best"));
    }

    //In order to save optimization time, we will same parameter estimates after each model was estimated
    //in order to use smart initial values for the next round...
    map<int, ParameterList> currentParameters;
    //At start, all nodes have the same parameter values (homogeneous model):
    vector<int> ids = ptree->getNodesId();
    ids.pop_back();
    for (size_t i = 0; i < ids.size(); ++i) {
      currentParameters[ids[i]] = model->getParameters();
    }

    //Now try more and more complex non-homogeneous models, using the clustering tree set as input.
    vector<const Node*> candidates;
    bool moveForward = true;
    map<double, vector<const Node*> >::iterator it = sortedHeights.begin();
    double currentThreshold = 1.;
    
    SubstitutionModelSet* modelSet = 0;
    SubstitutionModelSet* bestModelSet = 0;
    DiscreteDistribution* bestRDist = 0;
    TreeTemplate<Node>*   bestTree = 0;
    double bestAic = aic;
    double bestBic = bic;
    int previousBest = -1;
    vector< vector<int> > bestGroups;
    unsigned int modelCount = 0;
    while (moveForward && it != sortedHeights.end()) {
      modelCount++;
      for (vector<const Node*>::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
        for (unsigned int k = 0; k < (*it2)->getNumberOfSons(); ++k)
          candidates.push_back((*it2)->getSon(k));
      }
      currentThreshold = it->first;
      ApplicationTools::displayResult("Current threshold", currentThreshold);
      it++;
      
      //Get the corresponding partitions:
      vector< vector<int> > newGroups = getGroups(candidates);
      ApplicationTools::displayResult("Number of real partitions", newGroups.size());
      
      //Display partitions:
      for (size_t i = 0; i < newGroups.size(); ++i)
        ApplicationTools::displayResult("Partition " + TextTools::toString(i + 1), TextTools::toString(newGroups[i].size()) + " element(s).");

      //Now we have to build the corresponding model set:
      SubstitutionModelSet* newModelSet = buildModelSetFromPartitions(model, rootFreqs, ptree, newGroups, globalParameters, currentParameters);
      DiscreteDistribution* newRDist = rDist->clone();
      ParameterList previousParameters = drtl->getBranchLengthsParameters();
      previousParameters.addParameters(drtl->getRateDistributionParameters());
      delete drtl;
      if (dynamic_cast<MixedSubstitutionModel*>(model) == 0)
        drtl = new DRNonHomogeneousTreeLikelihood(*ptree, *sites, newModelSet, newRDist, false);
      else
        throw Exception("Mixed models not supported so far.");
        //drtl = new DRNonHomogeneousMixedTreeLikelihood(*ptree, *sites, modelSet, rDist, true);
      drtl->initialize();
      drtl->matchParametersValues(previousParameters); //This will save some time during optimization!
      
      //Optimize parameters
      messageHandler.reset(
        (mhPath == "none") ? 0 :
        (mhPath == "std") ? ApplicationTools::message :
        new StlOutputStream(auto_ptr<ostream>(new ofstream((mhPath + TextTools::toString(modelCount)).c_str(), ios::out))));

      profiler.reset(
        (prPath == "none") ? 0 :
        (prPath == "std") ? ApplicationTools::message :
        new StlOutputStream(auto_ptr<ostream>(new ofstream((prPath + TextTools::toString(modelCount)).c_str(), ios::out))));
      if (profiler.get()) profiler->setPrecision(20);

      //Reevaluate parameters, as there might be some change when going to a NH model:
      parametersToEstimate = getParametersToEstimate(drtl, partnh.getParams()); 
      
      estimateLikelihood(drtl, parametersToEstimate, tolerance, nbEvalMax, messageHandler.get(), profiler.get(), reparam, optVerbose);

      double newLogL = drtl->getValue();
      double newDf = static_cast<double>(drtl->getParameters().size());
      ApplicationTools::displayResult("* New NH model - LogL", -newLogL);
      ApplicationTools::displayResult("               - df", newDf);
      double newAic = 2. * (newDf + newLogL);
      double newBic = 2. * newLogL + newDf * log(nbSites);
      TreeTemplate<Node>* newTree = new TreeTemplate<Node>(drtl->getTree());

      double d = 2 * (logL - newLogL);
      double pvalue = 1. - RandomTools::pChisq(d, newDf - df);
      ApplicationTools::displayResult("               - LRT p-value", pvalue);
      ApplicationTools::displayResult("               - AIC", newAic);
      ApplicationTools::displayResult("               - BIC", newBic);

      if (logout.get())
        *logout << currentThreshold << "\t" << newGroups.size() << "\t" << -newLogL << "\t" << newDf << "\t" << newAic << "\t" << newBic << endl;

      //Finally compare new model to the current one:
      bool saveThisModel = false;
      bool test = false;
      if (likelihoodComparison == "LRT") {
        test = (pvalue <= testThreshold);
        if (test) {
          saveThisModel = true;
          moveForward = false;
        }
      } else {
        if (likelihoodComparison == "AIC") {
          test = (newAic < bestAic);
        } else { //BIC
          test = (newBic < bestBic);
        }
        if (newAic < bestAic)
          bestAic = newAic;
        if (newBic < bestBic)
          bestBic = newBic;
        if (test) {
          saveThisModel = true;
          previousBest = 0;
        } else {
          previousBest++;
        }
        if (previousBest == stop) {
          //We have to stop here
          moveForward = false;
        }
      }

      if (saveThisModel) {
        if (bestModelSet) {
          delete bestModelSet;
          delete bestRDist;
          delete bestTree;
        }
        bestModelSet = newModelSet->clone();
        bestRDist    = newRDist->clone();
        bestTree     = newTree->clone();
        bestGroups   = newGroups;
      }

      //Moving forward:
      if (moveForward) {
        delete ptree;
        if (modelSet)
          delete modelSet;
        delete rDist;
        ptree    = newTree;
        modelSet = newModelSet;
        rDist    = newRDist;
        logL     = newLogL;
        df       = newDf;
        aic      = newAic;
        bic      = newBic;
        groups   = newGroups;
        //Save parameters:
        for (size_t i = 0; i < ids.size(); ++i) {
          currentParameters[ids[i]] = modelSet->getModelForNode(ids[i])->getParameters();
        }
      } else {
        delete newModelSet;
        delete newRDist;
        delete newTree;
      }
    }
    //Write best model to file and output partition tree.
    ApplicationTools::displayResult("Model description output to file", modelPath);
    //We have to distinguish two cases...
    if (bestModelSet) {
      StlOutputStream out(auto_ptr<ostream>(new ofstream(modelPath.c_str(), ios::out)));
      out << "# Log likelihood = ";
      out.setPrecision(20) << (-drtl->getValue());
      out.endLine();
      out.endLine();
      out << "# Substitution model parameters:";
      out.endLine();
      PhylogeneticsApplicationTools::printParameters(bestModelSet, out);
      out.endLine();
      PhylogeneticsApplicationTools::printParameters(bestRDist   , out);
      out.endLine();
    } else {
      StlOutputStream out(auto_ptr<ostream>(new ofstream(modelPath.c_str(), ios::out)));
      out << "# Log likelihood = ";
      out.setPrecision(20) << (-drtl->getValue());
      out.endLine();
      out.endLine();
      out << "# Substitution model parameters:";
      out.endLine();
      PhylogeneticsApplicationTools::printParameters(model, out);
      out.endLine();
      PhylogeneticsApplicationTools::printParameters(rDist, out);
      out.endLine();
    }

    //Write parameter estimates per node:
    string paramPath = ApplicationTools::getAFilePath("output.parameters.file", partnh.getParams(), false, false);
    ApplicationTools::displayResult("Output parameter table to", paramPath);
    if (paramPath != "none") {
      ofstream paramFile(paramPath.c_str(), ios::out);
      vector<string> paramNames = model->getParameters().getParameterNames();
      paramFile << "NodeId";
      for (size_t i = 0; i < paramNames.size(); ++i)
        paramFile << "\t" << paramNames[i];
      paramFile << endl;
      if (bestModelSet) {
        for (unsigned k = 0; k < bestModelSet->getNumberOfModels(); ++k) {
          ParameterList pl = bestModelSet->getModel(k)->getParameters();
          vector<int> idsk = bestModelSet->getNodesWithModel(k);
          for (size_t j = 0; j < idsk.size(); ++j) {
            paramFile << idsk[j];
            for (size_t i = 0; i < paramNames.size(); ++i) {
              paramFile << "\t" << pl.getParameter(paramNames[i]).getValue();
            }
            paramFile << endl;
          }
        }
      } else {
        //All nodes have the same parameters:
        ParameterList pl = model->getParameters();
        for (size_t j = 0; j < ids.size(); ++j) {
          paramFile << ids[j];
          for (size_t i = 0; i < paramNames.size(); ++i) {
            paramFile << "\t" << pl.getParameter(paramNames[i]).getValue();
          }
          paramFile << endl;
        }
      }
      paramFile.close();
    }

    //Cleaning:
    if (bestTree) {
      ptree  = bestTree;
      groups = bestGroups;
    }
    delete bestModelSet;
    delete bestRDist;
  
    if (logout.get())
      logout->close();

  } else throw Exception("Unknown option: " + method);

  //Write best tree:
  PhylogeneticsApplicationTools::writeTree(*ptree, partnh.getParams());
 
  //Now write partitions to file:
  if (groups.size() > 1) {
    string partPath = ApplicationTools::getAFilePath("output.partitions.file", partnh.getParams(), true, false);
    ApplicationTools::displayResult("Partitions output to file", partPath);
    for (size_t i = 0; i < groups.size(); ++i) {
      for (size_t j = 0; j < groups[i].size(); ++j) {
        Node* node = ptree->getNode(groups[i][j]);
        if (node->hasName())
          node->setName(TextTools::toString(i + 1) + "_" + node->getName());
        node->setBranchProperty("partition", BppString(TextTools::toString(i + 1)));
      }
    }
    newick.enableExtendedBootstrapProperty("partition");
    newick.write(*ptree, partPath);
  } else {
    ApplicationTools::displayResult("Partitions output to file", string("None (no partitions found)"));
  }

  //Cleaning memory:
  delete htree;
  delete ptree;
  partnh.done();
  }
  catch (exception& e)
  {
    cout << e.what() << endl;
    exit(-1);
  }

  return (0);
}
