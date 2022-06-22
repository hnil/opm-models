// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/
/*!
 * \file
 *
 * \copydoc Opm::FvBaseLinearizer
 */
#ifndef EWOMS_LINEARIZER_TPFA_HH
#define EWOMS_LINEARIZER_TPFA_HH

#include "fvbaseproperties.hh"
#include "linearizationtype.hh"

#include <opm/models/parallel/gridcommhandles.hh>
#include <opm/models/parallel/threadmanager.hh>
#include <opm/models/parallel/threadedentityiterator.hh>
#include <opm/models/discretization/common/baseauxiliarymodule.hh>

#include <opm/material/common/Exceptions.hpp>

#include <dune/common/version.hh>
#include <dune/common/fvector.hh>
#include <dune/common/fmatrix.hh>

#include <opm/grid/utility/SparseTable.hpp>

#include <type_traits>
#include <iostream>
#include <vector>
#include <thread>
#include <set>
#include <exception>   // current_exception, rethrow_exception
#include <mutex>

namespace Opm {
// forward declarations
template<class TypeTag>
class EcfvDiscretization;

/*!
 * \ingroup FiniteVolumeDiscretizations
 *
 * \brief The common code for the linearizers of non-linear systems of equations
 *
 * This class assumes that these system of equations to be linearized are stemming from
 * models that use an finite volume scheme for spatial discretization and an Euler
 * scheme for time discretization.
 */
template<class TypeTag>
class LinearizerTPFA
{
//! \cond SKIP_THIS
    using Model = GetPropType<TypeTag, Properties::Model>;
    using Discretization = GetPropType<TypeTag, Properties::Discretization>;
    using Problem = GetPropType<TypeTag, Properties::Problem>;
    using Simulator = GetPropType<TypeTag, Properties::Simulator>;
    using GridView = GetPropType<TypeTag, Properties::GridView>;
    using Scalar = GetPropType<TypeTag, Properties::Scalar>;
    using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;
    using DofMapper = GetPropType<TypeTag, Properties::DofMapper>;
    using ElementMapper = GetPropType<TypeTag, Properties::ElementMapper>;
    using ElementContext = GetPropType<TypeTag, Properties::ElementContext>;

    using SolutionVector = GetPropType<TypeTag, Properties::SolutionVector>;
    using GlobalEqVector = GetPropType<TypeTag, Properties::GlobalEqVector>;
    using SparseMatrixAdapter = GetPropType<TypeTag, Properties::SparseMatrixAdapter>;
    using EqVector = GetPropType<TypeTag, Properties::EqVector>;
    using Constraints = GetPropType<TypeTag, Properties::Constraints>;
    using Stencil = GetPropType<TypeTag, Properties::Stencil>;
    using ThreadManager = GetPropType<TypeTag, Properties::ThreadManager>;
    using LocalResidual = GetPropType<TypeTag, Properties::LocalResidual>;
    using IntensiveQuantities = GetPropType<TypeTag, Properties::IntensiveQuantities>;
    using GridCommHandleFactory = GetPropType<TypeTag, Properties::GridCommHandleFactory>;

    using Toolbox = MathToolbox<Evaluation>;

    using Element = typename GridView::template Codim<0>::Entity;
    using ElementIterator = typename GridView::template Codim<0>::Iterator;

    using Vector = GlobalEqVector;

    using IstlMatrix = typename SparseMatrixAdapter::IstlMatrix;

    enum { numEq = getPropValue<TypeTag, Properties::NumEq>() };
    enum { historySize = getPropValue<TypeTag, Properties::TimeDiscHistorySize>() };

    using MatrixBlock = typename SparseMatrixAdapter::MatrixBlock;
    using VectorBlock = Dune::FieldVector<Scalar, numEq>;
    using ADVectorBlock = GetPropType<TypeTag, Properties::RateVector>;
    //Dune::FieldVector<Evaluation, numEq>;
    
    static const bool linearizeNonLocalElements = getPropValue<TypeTag, Properties::LinearizeNonLocalElements>();

    // copying the linearizer is not a good idea
    LinearizerTPFA(const LinearizerTPFA&);
//! \endcond

public:
    LinearizerTPFA()
        : jacobian_()
    {
        simulatorPtr_ = 0;
    }

    ~LinearizerTPFA()
    {
        auto it = elementCtx_.begin();
        const auto& endIt = elementCtx_.end();
        for (; it != endIt; ++it)
            delete *it;
    }

    /*!
     * \brief Register all run-time parameters for the Jacobian linearizer.
     */
    static void registerParameters()
    { }

    /*!
     * \brief Initialize the linearizer.
     *
     * At this point we can assume that all objects in the simulator
     * have been allocated. We cannot assume that they are fully
     * initialized, though.
     *
     * \copydetails Doxygen::simulatorParam
     */
    void init(Simulator& simulator)
    {
        simulatorPtr_ = &simulator;
        eraseMatrix();
        auto it = elementCtx_.begin();
        const auto& endIt = elementCtx_.end();
        for (; it != endIt; ++it){
            delete *it;
        }
        elementCtx_.resize(0);
    }

    /*!
     * \brief Causes the Jacobian matrix to be recreated from scratch before the next
     *        iteration.
     *
     * This method is usally called if the sparsity pattern has changed for some
     * reason. (e.g. by modifications of the grid or changes of the auxiliary equations.)
     */
    void eraseMatrix()
    {
        jacobian_.reset();
    }

    /*!
     * \brief Linearize the full system of non-linear equations.
     *
     * The linearizationType() controls the scheme used and the focus
     * time index. The default is fully implicit scheme, and focus index
     * equal to 0, i.e. current time (end of step).
     *
     * This linearizes the spatial domain and all auxiliary equations.
     */
    void linearize()
    {
        linearizeDomain();
        linearizeAuxiliaryEquations();
    }

    /*!
     * \brief Linearize the part of the non-linear system of equations that is associated
     *        with the spatial domain.
     *
     * That means that the global Jacobian of the residual is assembled and the residual
     * is evaluated for the current solution.
     *
     * The current state of affairs (esp. the previous and the current solutions) is
     * represented by the model object.
     */
    void linearizeDomain()
    {
        // we defer the initialization of the Jacobian matrix until here because the
        // auxiliary modules usually assume the problem, model and grid to be fully
        // initialized...
        if (!jacobian_)
            initFirstIteration_();

        int succeeded;
        try {
            //linearize_();
            linearizeGlobalTPFA_();
            succeeded = 1;
        }
        catch (const std::exception& e)
        {
            std::cout << "rank " << simulator_().gridView().comm().rank()
                      << " caught an exception while linearizing:" << e.what()
                      << "\n"  << std::flush;
            succeeded = 0;
        }
        catch (...)
        {
            std::cout << "rank " << simulator_().gridView().comm().rank()
                      << " caught an exception while linearizing"
                      << "\n"  << std::flush;
            succeeded = 0;
        }
        succeeded = gridView_().comm().min(succeeded);

        if (!succeeded)
            throw NumericalIssue("A process did not succeed in linearizing the system");
    }

    void finalize()
    { jacobian_->finalize(); }

    /*!
     * \brief Linearize the part of the non-linear system of equations that is associated
     *        with the spatial domain.
     */
    void linearizeAuxiliaryEquations()
    {
        // flush possible local caches into matrix structure
        jacobian_->commit();

        auto& model = model_();
        const auto& comm = simulator_().gridView().comm();
        for (unsigned auxModIdx = 0; auxModIdx < model.numAuxiliaryModules(); ++auxModIdx) {
            bool succeeded = true;
            try {
                model.auxiliaryModule(auxModIdx)->linearize(*jacobian_, residual_);
            }
            catch (const std::exception& e) {
                succeeded = false;

                std::cout << "rank " << simulator_().gridView().comm().rank()
                          << " caught an exception while linearizing:" << e.what()
                          << "\n"  << std::flush;
            }

            succeeded = comm.min(succeeded);

            if (!succeeded)
                throw NumericalIssue("linearization of an auxiliary equation failed");
        }
    }

    /*!
     * \brief Return constant reference to global Jacobian matrix backend.
     */
    const SparseMatrixAdapter& jacobian() const
    { return *jacobian_; }

    SparseMatrixAdapter& jacobian()
    { return *jacobian_; }

    /*!
     * \brief Return constant reference to global residual vector.
     */
    const GlobalEqVector& residual() const
    { return residual_; }

    GlobalEqVector& residual()
    { return residual_; }

    void setLinearizationType(LinearizationType linearizationType){
        linearizationType_ = linearizationType;
    };

    const LinearizationType& getLinearizationType() const{
        return linearizationType_;
    };

    /*!
     * \brief Returns the map of constraint degrees of freedom.
     *
     * (This object is only non-empty if the EnableConstraints property is true.)
     */
    const std::map<unsigned, Constraints>& constraintsMap() const
    { return constraintsMap_; }

private:
    Simulator& simulator_()
    { return *simulatorPtr_; }
    const Simulator& simulator_() const
    { return *simulatorPtr_; }

    Problem& problem_()
    { return simulator_().problem(); }
    const Problem& problem_() const
    { return simulator_().problem(); }

    Model& model_()
    { return simulator_().model(); }
    const Model& model_() const
    { return simulator_().model(); }

    const GridView& gridView_() const
    { return problem_().gridView(); }

    const ElementMapper& elementMapper_() const
    { return model_().elementMapper(); }

    const DofMapper& dofMapper_() const
    { return model_().dofMapper(); }

    void initFirstIteration_()
    {
        // initialize the BCRS matrix for the Jacobian of the residual function
        createMatrix_();

        // initialize the Jacobian matrix and the vector for the residual function
        residual_.resize(model_().numTotalDof());
        resetSystem_();

        // create the per-thread context objects
        elementCtx_.resize(ThreadManager::maxThreads());
        for (unsigned threadId = 0; threadId != ThreadManager::maxThreads(); ++ threadId)
            elementCtx_[threadId] = new ElementContext(simulator_());
    }

    // Construct the BCRS matrix for the Jacobian of the residual function
    void createMatrix_()
    {
        const auto& model = model_();
        Stencil stencil(gridView_(), model_().dofMapper());

        // for the main model, find out the global indices of the neighboring degrees of
        // freedom of each primary degree of freedom
        using NeighborSet = std::set< unsigned >;
        std::vector<NeighborSet> sparsityPattern(model.numTotalDof());

        ElementIterator elemIt = gridView_().template begin<0>();
        const ElementIterator elemEndIt = gridView_().template end<0>();
        for (; elemIt != elemEndIt; ++elemIt) {
            const Element& elem = *elemIt;
            stencil.update(elem);

            for (unsigned primaryDofIdx = 0; primaryDofIdx < stencil.numPrimaryDof(); ++primaryDofIdx) {
                unsigned myIdx = stencil.globalSpaceIndex(primaryDofIdx);

                for (unsigned dofIdx = 0; dofIdx < stencil.numDof(); ++dofIdx) {
                    unsigned neighborIdx = stencil.globalSpaceIndex(dofIdx);
                    sparsityPattern[myIdx].insert(neighborIdx);
                }
            }
        }

        // add the additional neighbors and degrees of freedom caused by the auxiliary
        // equations
        auto reservoirSparsityPattern = sparsityPattern; 
        size_t numAuxMod = model.numAuxiliaryModules();
        for (unsigned auxModIdx = 0; auxModIdx < numAuxMod; ++auxModIdx)
            model.auxiliaryModule(auxModIdx)->addNeighbors(sparsityPattern);

        // allocate raw matrix
        jacobian_.reset(new SparseMatrixAdapter(simulator_()));

        // create matrix structure based on sparsity pattern
        jacobian_->reserve(sparsityPattern);
        //neighbours_ = sparsityPattern;
        for(unsigned globI= 0; globI < model.numTotalDof(); globI++){
            reservoirSparsityPattern[globI].erase(globI);
        }
        unsigned numCells = model.numTotalDof();
        neighbours_.reserve(numCells,6*numCells);
        trans_.reserve(numCells,6*numCells);
        std::vector<double> loctrans;
        for(unsigned globI= 0; globI < numCells; globI++){
            const auto&  cells = reservoirSparsityPattern[globI];
            neighbours_.appendRow(cells.begin(),cells.end());
            unsigned n = cells.size();
            loctrans.resize(n);
            short loc = 0;
            for(const int& cell : cells){
                loctrans[loc] = problem_().transmissibility(globI, cell); 
                loc ++;
            }
            trans_.appendRow(loctrans.begin(),loctrans.end());
        }
    }

    // reset the global linear system of equations.
    void resetSystem_()
    {
        residual_ = 0.0;
        // zero all matrix entries
        jacobian_->clear();
    }

    // query the problem for all constraint degrees of freedom. note that this method is
    // quite involved and is thus relatively slow.
    void updateConstraintsMap_()
    {
        if (!enableConstraints_())
            // constraints are not explictly enabled, so we don't need to consider them!
            return;

        constraintsMap_.clear();

        // loop over all elements...
        ThreadedEntityIterator<GridView, /*codim=*/0> threadedElemIt(gridView_());
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            unsigned threadId = ThreadManager::threadId();
            ElementIterator elemIt = threadedElemIt.beginParallel();
            for (; !threadedElemIt.isFinished(elemIt); elemIt = threadedElemIt.increment()) {
                // create an element context (the solution-based quantities are not
                // available here!)
                const Element& elem = *elemIt;
                ElementContext& elemCtx = *elementCtx_[threadId];
                elemCtx.updateStencil(elem);

                // check if the problem wants to constrain any degree of the current
                // element's freedom. if yes, add the constraint to the map.
                for (unsigned primaryDofIdx = 0;
                     primaryDofIdx < elemCtx.numPrimaryDof(/*timeIdx=*/0);
                     ++ primaryDofIdx)
                {
                    Constraints constraints;
                    elemCtx.problem().constraints(constraints,
                                                  elemCtx,
                                                  primaryDofIdx,
                                                  /*timeIdx=*/0);
                    if (constraints.isActive()) {
                        unsigned globI = elemCtx.globalSpaceIndex(primaryDofIdx, /*timeIdx=*/0);
                        constraintsMap_[globI] = constraints;
                        continue;
                    }
                }
            }
        }
    }

public:
    void setResAndJacobi(VectorBlock& res,MatrixBlock& bMat,const ADVectorBlock& resid) const{
        for (unsigned eqIdx = 0; eqIdx < numEq; eqIdx++)
            res[eqIdx] = resid[eqIdx].value();
        
        for (unsigned eqIdx = 0; eqIdx < numEq; eqIdx++) {
            for (unsigned pvIdx = 0; pvIdx < numEq; pvIdx++) {
                    // A[dofIdx][focusDofIdx][eqIdx][pvIdx] is the partial derivative of
                    // the residual function 'eqIdx' for the degree of freedom 'dofIdx'
                    // with regard to the focus variable 'pvIdx' of the degree of freedom
                    // 'focusDofIdx'
                    bMat[eqIdx][pvIdx] = resid[eqIdx].derivative(pvIdx);
                }
        }
    }
private:
    void linearizeGlobalTPFA_()
    {
        const bool well_local = false;
        resetSystem_();
        unsigned numCells = model_().numTotalDof();
#ifdef _OPENMP
#pragma omp parallel for
#endif        
        for(unsigned globI = 0; globI < numCells; globI++){
            const auto& neighbours = neighbours_[globI];// this is a set but should maybe be changed
            // accumulation term
            double dt = simulator_().timeStepSize();
            double volume = model_().dofTotalVolume(globI);
            Scalar storefac = volume/dt;           
            ADVectorBlock adres(0.0);
            const IntensiveQuantities* intQuantsInP = model_().cachedIntensiveQuantities(globI, /*timeIdx*/0);
            assert(intQuantsInP);
            const IntensiveQuantities& intQuantsIn = *intQuantsInP;
            //intensiveQuantity(globI, 0);
            LocalResidual::computeStorage(adres,intQuantsIn, 0);           
            adres *= storefac;
            VectorBlock res (0.0);
            MatrixBlock bMat(0.0);
            setResAndJacobi(res,bMat, adres);
            // first we use it as storage cache
            if (model_().newtonMethod().numIterations() == 0){
                model_().updateCachedStorage(globI, /*timeIdx=*/1, res);
            }
            residual_[globI] -= model_().cachedStorage(globI, 1);//*storefac;
            residual_[globI] += res;
            jacobian_->addToBlock(globI, globI, bMat);
            // wells sources for now (should be moved out)
            if(well_local){
                res = 0.0;
                bMat = 0.0;
                adres = 0.0;
                LocalResidual::computeSource(adres, problem_(), globI, 0);
                adres *= -volume;
                setResAndJacobi(res, bMat, adres);            
                residual_[globI] += res;
                jacobian_->addToBlock(globI, globI, bMat);
            }
            short loc = 0;
            for(const auto& globJ: neighbours){
                assert(globJ != globI);
                res = 0.0;
                bMat = 0.0;
                adres = 0.0;
                const IntensiveQuantities* intQuantsExP = model_().cachedIntensiveQuantities(globJ, /*timeIdx*/0);
                assert(intQuantsExP);
                const IntensiveQuantities& intQuantsEx = *intQuantsExP;
                unsigned globalFocusDofIdx = globI;
                LocalResidual::computeFlux(adres,
                                           problem_(),
                                           globalFocusDofIdx,
                                           globI,
                                           globJ,
                                           intQuantsIn,
                                           intQuantsEx,
                                           0);
                adres *= trans_[globI][loc];
                setResAndJacobi(res, bMat, adres);    
                residual_[globI] += res;
                jacobian_->addToBlock(globI, globI, bMat);
                bMat *= -1.0;
                jacobian_->addToBlock(globJ, globI, bMat);
                loc ++;
            }
            
        }
        if(not(well_local)){
                problem_().wellModel().addReseroirSourceTerms(residual_,*jacobian_);
        }
        // before the first iteration of each time step, we need to update the
        // constraints. (i.e., we assume that constraints can be time dependent, but they
        // can't depend on the solution.)
    }
    

    // linearize the whole system
    void linearize_()
    {
        resetSystem_();

        // before the first iteration of each time step, we need to update the
        // constraints. (i.e., we assume that constraints can be time dependent, but they
        // can't depend on the solution.)
        if (model_().newtonMethod().numIterations() == 0)
            updateConstraintsMap_();

        applyConstraintsToSolution_();

        // to avoid a race condition if two threads handle an exception at the same time,
        // we use an explicit lock to control access to the exception storage object
        // amongst thread-local handlers
        std::mutex exceptionLock;

        // storage to any exception that needs to be bridged out of the
        // parallel block below. initialized to null to indicate no exception
        std::exception_ptr exceptionPtr = nullptr;

        // relinearize the elements...
        ThreadedEntityIterator<GridView, /*codim=*/0> threadedElemIt(gridView_());
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
            ElementIterator elemIt = threadedElemIt.beginParallel();
            ElementIterator nextElemIt = elemIt;
            try {
                for (; !threadedElemIt.isFinished(elemIt); elemIt = nextElemIt) {
                    // give the model and the problem a chance to prefetch the data required
                    // to linearize the next element, but only if we need to consider it
                    nextElemIt = threadedElemIt.increment();
                    if (!threadedElemIt.isFinished(nextElemIt)) {
                        const auto& nextElem = *nextElemIt;
                        if (linearizeNonLocalElements
                            || nextElem.partitionType() == Dune::InteriorEntity)
                        {
                            model_().prefetch(nextElem);
                            problem_().prefetch(nextElem);
                        }
                    }

                    const Element& elem = *elemIt;
                    if (!linearizeNonLocalElements && elem.partitionType() != Dune::InteriorEntity)
                        continue;

                    linearizeElement_(elem);
                }
            }
            // If an exception occurs in the parallel block, it won't escape the
            // block; terminate() is called instead of a handler outside!  hence, we
            // tuck any exceptions that occur away in the pointer. If an exception
            // occurs in more than one thread at the same time, we must pick one of
            // them to be rethrown as we cannot have two active exceptions at the
            // same time. This solution essentially picks one at random. This will
            // only be a problem if two different kinds of exceptions are thrown, for
            // instance if one thread experiences a (recoverable) numerical issue
            // while another is out of memory.
            catch(...) {
                std::lock_guard<std::mutex> take(exceptionLock);
                exceptionPtr = std::current_exception();
                threadedElemIt.setFinished();
            }
        }  // parallel block

        // after reduction from the parallel block, exceptionPtr will point to
        // a valid exception if one occurred in one of the threads; rethrow
        // it here to let the outer handler take care of it properly
        if(exceptionPtr) {
            std::rethrow_exception(exceptionPtr);
        }

        applyConstraintsToLinearization_();
    }

    // linearize an element in the interior of the process' grid partition
    void linearizeElement_(const Element& elem)
    {
        unsigned threadId = ThreadManager::threadId();

        ElementContext *elementCtx = elementCtx_[threadId];
        auto& localLinearizer = model_().localLinearizer(threadId);

        // the actual work of linearization is done by the local linearizer class
        localLinearizer.linearize(*elementCtx, elem);

        // update the right hand side and the Jacobian matrix
        if (getPropValue<TypeTag, Properties::UseLinearizationLock>())
            globalMatrixMutex_.lock();

        size_t numPrimaryDof = elementCtx->numPrimaryDof(/*timeIdx=*/0);
        for (unsigned primaryDofIdx = 0; primaryDofIdx < numPrimaryDof; ++ primaryDofIdx) {
            unsigned globI = elementCtx->globalSpaceIndex(/*spaceIdx=*/primaryDofIdx, /*timeIdx=*/0);

            // update the right hand side
            residual_[globI] += localLinearizer.residual(primaryDofIdx);

            // update the global Jacobian matrix
            for (unsigned dofIdx = 0; dofIdx < elementCtx->numDof(/*timeIdx=*/0); ++ dofIdx) {
                unsigned globJ = elementCtx->globalSpaceIndex(/*spaceIdx=*/dofIdx, /*timeIdx=*/0);

                jacobian_->addToBlock(globJ, globI, localLinearizer.jacobian(dofIdx, primaryDofIdx));
            }
        }

        if (getPropValue<TypeTag, Properties::UseLinearizationLock>())
            globalMatrixMutex_.unlock();
    }

    // apply the constraints to the solution. (i.e., the solution of constraint degrees
    // of freedom is set to the value of the constraint.)
    void applyConstraintsToSolution_()
    {
        if (!enableConstraints_())
            return;

        // TODO: assuming a history size of 2 only works for Euler time discretizations!
        auto& sol = model_().solution(/*timeIdx=*/0);
        auto& oldSol = model_().solution(/*timeIdx=*/1);

        auto it = constraintsMap_.begin();
        const auto& endIt = constraintsMap_.end();
        for (; it != endIt; ++it) {
            sol[it->first] = it->second;
            oldSol[it->first] = it->second;
        }
    }

    // apply the constraints to the linearization. (i.e., for constrain degrees of
    // freedom the Jacobian matrix maps to identity and the residual is zero)
    void applyConstraintsToLinearization_()
    {
        if (!enableConstraints_())
            return;

        auto it = constraintsMap_.begin();
        const auto& endIt = constraintsMap_.end();
        for (; it != endIt; ++it) {
            unsigned constraintDofIdx = it->first;

            // reset the column of the Jacobian matrix
            // put an identity matrix on the main diagonal of the Jacobian
            jacobian_->clearRow(constraintDofIdx, Scalar(1.0));

            // make the right-hand side of constraint DOFs zero
            residual_[constraintDofIdx] = 0.0;
        }
    }

    static bool enableConstraints_()
    { return getPropValue<TypeTag, Properties::EnableConstraints>(); }

    Simulator *simulatorPtr_;
    std::vector<ElementContext*> elementCtx_;

    // The constraint equations (only non-empty if the
    // EnableConstraints property is true)
    std::map<unsigned, Constraints> constraintsMap_;

    // the jacobian matrix
    std::unique_ptr<SparseMatrixAdapter> jacobian_;

    // the right-hand side
    GlobalEqVector residual_;

    LinearizationType linearizationType_;

    std::mutex globalMatrixMutex_;
    //using NeighborSet = std::set< unsigned >;
    //std::vector< std::vector<int>>
    SparseTable<unsigned> neighbours_;
    //std::vector< std::vector<double>> trans_;
    SparseTable<double> trans_;
};

} // namespace Opm

#endif