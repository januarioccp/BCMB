#include "minknap.h"

#include <ilcplex/ilocplex.h>

#include <stdexcept>
#include <iostream>
#include <fstream>
#include <vector>
using namespace std;

#define EPSILON 1e-6

// Read data from filename
static void readData(const char *filename, IloNum &binCapacity, IloNumArray &itemWeight);
static void masterDebug(IloCplex &binPackingSolver, IloNumVarArray Lambda, IloRangeArray Fill);
static void subDebug(IloAlgorithm &patSolver, IloNumVarArray Use, IloObjective obj);
static void resultDebug(IloCplex &binPackingSolver, IloNumVarArray Lambda);
vector<vector<bool>> bin;

int main(int argc, char **argv)
{
   /**
    * User env to manage memory and identify modeling objects.
    * */
   IloEnv env;
   try
   {
      /**
       * Stores the capacity of my bin
       * */
      IloNum binCapacity;

      /**
       * Stores the weight of each item
       *  */
      IloNumArray itemWeight(env);

      /**
       * Read the input data from the data file passed as argument to the program
       * */
      if (argc > 1)
         readData(argv[1], binCapacity, itemWeight);
      else
         throw invalid_argument("Plese, give me an input file");

      IloInt nItems = itemWeight.getSize();

      int *x = new int[nItems];
      int *p = new int[nItems];
      int *w = new int[nItems];

      for (int i = 0; i < nItems; i++)
         w[i] = itemWeight[i];

      /**
       * Declare:
       * - array of variables
       * - range of constraints
       * - an objective function
       * - a model for the master problem
       * */
      IloNumVarArray Lambda(env, nItems, 0, IloInfinity);
      IloRangeArray Fill(env);

      IloModel masterBinPacking(env);

      /**
       * Define variables
       * */
      for (int i = 0; i < nItems; i++)
         Lambda[i].setName(("L_" + to_string(i)).c_str());

      /**
       * Define range
       * */
      for (int i = 0; i < nItems; i++)
         Fill.add(Lambda[i] == 1);
      masterBinPacking.add(Fill);

      // One item in each bin
      bin = vector<vector<bool>>(nItems, vector<bool>(nItems, false));
      // Item i is in bin i
      for (int i = 0; i < nItems; i++)
         bin[i][i] = true;

      /**
       * Define objective function
       * */
      IloObjective binsUsed = IloAdd(masterBinPacking, IloMinimize(env, IloSum(Lambda)));

      IloCplex binPackingSolver(masterBinPacking);
      binPackingSolver.setOut(env.getNullStream());

      /// PATTERN-GENERATION PROBLEM ///

      IloModel patGen(env);

      IloObjective ReducedCost = IloAdd(patGen, IloMinimize(env, 1));
      IloNumVarArray Use(env, nItems, 0.0, 1, ILOINT);
      patGen.add(IloScalProd(itemWeight, Use) <= binCapacity);

      IloCplex patSolver(patGen);
      patSolver.setOut(env.getNullStream());

      /// COLUMN-GENERATION PROCEDURE ///
      IloNumArray price(env, nItems);
      IloNumArray newPatt(env, nItems);

      int k = 2;
      while (true)
      {
         /// OPTIMIZE OVER CURRENT PATTERNS ///

         binPackingSolver.solve();
         // masterDebug (binPackingSolver, Lambda, Fill);

         /// FIND AND ADD A NEW PATTERN ///
#ifndef MOCHILA_MODEL
         double factor = 1000000;
         for (int i = 0; i < nItems; i++)
            p[i] = (binPackingSolver.getDual(Fill[i]) > 0) ? factor * binPackingSolver.getDual(Fill[i]) : 0;

         double rc = 1.0 - minknap(nItems, p, w, x, binCapacity) / factor;
         // cout << "Knapsack cost:  " << fixed << rc << endl;

         if (rc > -EPSILON)
         {
            break;
         }
         for (int i = 0; i < nItems; i++)
            newPatt[i] = x[i];
#else
         for (int i = 0; i < nItems; i++)
            price[i] = -binPackingSolver.getDual(Fill[i]);

         ReducedCost.setLinearCoefs(Use, price);

         patSolver.solve();

         if (patSolver.getValue(ReducedCost) > -EPSILON)
            break;
         
         patSolver.getValues(newPatt, Use);
#endif
         Lambda.add(IloNumVar(binsUsed(1) + Fill(newPatt)));

         // Memorize the new colum
         bin.push_back(vector<bool>(nItems, false));
         for (int i = 0; i < nItems; i++)
            bin.back()[i] = (newPatt[i] > 0.9) ? true : false;
      }

      masterBinPacking.add(IloConversion(env, Lambda, ILOINT));

      binPackingSolver.solve();
      binPackingSolver.setOut(cout);
      cout << "Solution status: " << binPackingSolver.getStatus() << endl;
      resultDebug(binPackingSolver, Lambda);

      delete[] x;
      delete[] p;
   }
   catch (IloException &ex)
   {
      cerr << "Error: " << ex << endl;
   }
   catch (const std::invalid_argument &ia)
   {
      cerr << "Error: " << ia.what() << endl;
   }
   catch (...)
   {
      cerr << "Error" << endl;
   }

   env.end();
   return 0;
}

static void readData(const char *filename, IloNum &binCapacity,
                     IloNumArray &itemWeight)
{
   ifstream in(filename);
   if (in)
   {
      int quantity;
      in >> quantity;
      in >> binCapacity;
      int weight;
      for (int i = 0; i < quantity; i++)
      {
         in >> weight;
         itemWeight.add(weight);
      }
   }
   else
   {
      cerr << "No such file: " << filename << endl;
      throw(1);
   }
}

static void masterDebug(IloCplex &binPackingSolver, IloNumVarArray Lambda,
                        IloRangeArray Fill)
{
   cout << endl;
   cout << "Using " << binPackingSolver.getObjValue() << " bins" << endl;
   cout << endl;
   for (int64_t j = 0; j < Lambda.getSize(); j++)
      cout << "  Lambda" << j << " = " << binPackingSolver.getValue(Lambda[j]) << endl;

   cout << endl;
   for (int i = 0; i < Fill.getSize(); i++)
      cout << "  Fill" << i << " = " << binPackingSolver.getDual(Fill[i]) << endl;

   cout << endl;
}

static void subDebug(IloAlgorithm &patSolver, IloNumVarArray Use,
                     IloObjective obj)
{
   // cout << endl;
   cout << "Reduced cost is " << patSolver.getValue(obj) << endl;
   cout << endl;
   // if (patSolver.getValue(obj) <= 0)
   // {
   //    for (IloInt i = 0; i < Use.getSize(); i++)

   //       cout << "  Use" << i << " = " << patSolver.getValue(Use[i]) << endl;

   //    cout << endl;
   // }
}

static void resultDebug(IloCplex &binPackingSolver, IloNumVarArray Lambda)
{
   cout << endl;
   cout << "Best solution uses "
        << binPackingSolver.getObjValue() << " bins" << endl;
   int b = 1;
   for (IloInt k = 0; k < Lambda.getSize(); k++)
   {
      if (binPackingSolver.getValue(Lambda[k]) > 1.0 - EPSILON)
      {
         cout << "Bin[" << b << "] = ";
         for (int i = 0; i < bin[k].size(); i++)
            if (bin[k][i] == true)
               cout << i + 1 << " ";
         cout << endl;
         b++;
      }
   }
}
