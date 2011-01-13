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
    const vector<string>& globalParameterNames
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
    modelSet->addModel(dynamic_cast<SubstitutionModel*>(model->clone()),
        groups[i], branchParameters.getParameterNames());
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

int main(int args, char ** argv)
{
  cout << "******************************************************************" << endl;
  cout << "*                     PartNH, version 0.1.0                      *" << endl;
  cout << "* Authors: J. Dutheil                       Created on  09/12/10 *" << endl;
  cout << "*          B. Boussau                       Last Modif. 20/03/08 *" << endl;
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
    
    //Optimize parameters
    PhylogeneticsApplicationTools::optimizeParameters(drtl, drtl->getParameters(), partnh.getParams(), "", true, true);
    double logL = drtl->getValue();
    double df = static_cast<double>(drtl->getParameters().size());
    double aic = 2. * (df + logL);
    double bic = 2. * logL + df * log(nbSites);
    ApplicationTools::displayResult("* Homogeneous model - LogL", logL);
    ApplicationTools::displayResult("                    - df", df);
    
    //Get necessary things for building a non-homogeneous model:
    vector<double> rateFreqs;
    if (model->getNumberOfStates() != alphabet->getSize())
    {
      // Markov-Modulated Markov Model...
      unsigned int n = (unsigned int)(model->getNumberOfStates() / alphabet->getSize());
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

    //Now try more and more complex non-homogeneous models, using the clustering tree set as input.
    vector<const Node*> candidates;
    bool test = true;
    map<double, vector<const Node*> >::iterator it = sortedHeights.begin();
    double currentThreshold = 1.;
    

    SubstitutionModelSet* modelSet = 0;
    while (test && it != sortedHeights.end()) {
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
      SubstitutionModelSet* newModelSet = buildModelSetFromPartitions(model, rootFreqs, ptree, newGroups, globalParameters);
      ParameterList previousParameters = drtl->getBranchLengthsParameters();
      previousParameters.addParameters(drtl->getRateDistributionParameters());
      delete drtl;
      if (dynamic_cast<MixedSubstitutionModel*>(model) == 0)
        drtl = new DRNonHomogeneousTreeLikelihood(*ptree, *sites, newModelSet, rDist, false);
      else
        throw Exception("Mixed models not supported so far.");
        //drtl = new DRNonHomogeneousMixedTreeLikelihood(*ptree, *sites, modelSet, rDist, true);
      drtl->initialize();
      drtl->matchParametersValues(previousParameters); //This will save some time during optimization!
      //Optimize parameters
      PhylogeneticsApplicationTools::optimizeParameters(drtl, drtl->getParameters(), partnh.getParams(), "", true, false);
      double newLogL = drtl->getValue();
      double newDf = static_cast<double>(drtl->getParameters().size());
      ApplicationTools::displayResult("* New NH model - LogL", newLogL);
      ApplicationTools::displayResult("               - df", newDf);
      double newAic = 2. * (newDf + newLogL);
      double newBic = 2. * newLogL + newDf * log(nbSites);

      double d = 2 * (logL - newLogL);
      double pvalue = 1. - RandomTools::pChisq(d, newDf - df);
      ApplicationTools::displayResult("               - LRT p-value", pvalue);
      ApplicationTools::displayResult("               - AIC", newAic);
      ApplicationTools::displayResult("               - BIC", newBic);

      //Finally compare new model to the current one:
      if (likelihoodComparison == "LRT") {
        test = (pvalue <= testThreshold);
      } else if (likelihoodComparison == "AIC") {
        test = (newAic < aic);
      } else { //BIC
        test = (newBic < bic);
      }

      if (test) {
        delete ptree;
        ptree = new TreeTemplate<Node>(drtl->getTree());
        if (modelSet)
          delete modelSet;
        modelSet = newModelSet;
        logL     = newLogL;
        df       = newDf;
        aic      = newAic;
        bic      = newBic;
        groups   = newGroups;
      } else {
        delete newModelSet;
      }
    }
    //Write best model to file and output partition tree.
    ApplicationTools::displayResult("Model description output to file", modelPath);
    //We have to distinguish two cases...
    if (modelSet) {
      StlOutputStream out(auto_ptr<ostream>(new ofstream(modelPath.c_str(), ios::out)));
      out << "# Log likelihood = ";
      out.setPrecision(20) << (-drtl->getValue());
      out.endLine();
      out.endLine();
      out << "# Substitution model parameters:";
      out.endLine();
      PhylogeneticsApplicationTools::printParameters(modelSet, out);
      out.endLine();
      PhylogeneticsApplicationTools::printParameters(rDist   , out);
      out.endLine();
    } else {
      StlOutputStream out(auto_ptr<ostream>(new ofstream("model.bpp", ios::out)));
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
