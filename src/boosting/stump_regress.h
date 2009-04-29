/* stump_regress.h                                                 -*- C++ -*-
   Jeremy Barnes, 30 March 2004
   Copyright (c) 2004 Jeremy Barnes.  All rights reserved.
   $Source$

   Regression training for the decision stumps.  Includes a W data structure
   for the accumulation, a Z object to calculate the split score, and a
   C object to give the final distributions.
*/

#ifndef __boosting__stump_regress_h__
#define __boosting__stump_regress_h__

#include <string>
#include <vector>
#include "stats/distribution.h"
#include <boost/multi_array.hpp>
#include "stump_training.h"
#include "training_data.h"
#include "training_index.h"
#include <numeric>



namespace ML {


/*****************************************************************************/
/* W ARRAY                                                                   */
/*****************************************************************************/

struct W_regress {
    W_regress(size_t nl)
    {
        if (nl != 1)
            throw Exception("W_regress::W_regress(): not a regression problem");
        for (unsigned i = 0;  i < 3;  ++i)
            dist[i] = sqr[i] = wt[i] = 0.0;
    }

    void swap(W_regress & other)
    {
        std::swap(*this, other);
    }

    std::string print() const
    {
        return "W_regress";
    }
    
    size_t nl() const { return 1; }

    /** Add weight to a bucket over all labels.
        \param correct_label The correct label for this training sample.
        \param bucket       The bucket add transfer weight to.
        \param it           An iterator pointing to the start of the range of
                            sample weights.
    */
    template<class Iterator>
    void add(Label correct_label, int bucket, Iterator it,
             int advance)
    {
        add(correct_label, bucket, 1.0, it, advance);
    }

    /** Add weight to a bucket over all labels, weighted.
        \param correct_label The correct label for this training sample.
        \param bucket       The bucket add transfer weight to.
        \param weight       The weight to add it with.
        \param it           An iterator pointing to the start of the range of
                            sample weights.
    */
    template<class Iterator>
    void add(Label correct_label, int bucket, float weight,
             Iterator it, int advance)
    {
        float f = correct_label.value();
        float w = *it * weight;
        float fw = f * w;
        float ffw = f * fw;
        dist[bucket] += fw;
        sqr[bucket] += ffw;
        wt[bucket] += w;
    }

    /** Transfer weight from one bucket to another over all labels.
        \param label        The correct label for this training sample.
        \param from         The bucket to transfer weight from.
        \param to           The bucket to transfer weight to.
        \param weight       The amount by which to scale the weights from
                            the weight array.
        \param it           An iterator pointing to the start of the range of
                            sample weights.
    */
    template<class Iterator>
    void transfer(Label correct_label, int from, int to,
                  float weight, Iterator it, int advance)
    {
        float f = correct_label.value();
        float w = *it * weight;
        float fw = f * w;
        float ffw = f * fw;
        dist[from] -= fw;
        sqr[from] -= ffw;
        wt[from] -= w;
        dist[to] += fw;
        sqr[to] += ffw;
        wt[to] += w;
    }

    void transfer(int from, int to, const W_regress & w)
    {
        throw Exception("W_regress::transfer() not implemented");
    }
    
    /** This function ensures that the values in the MISSING bucket are all
        greater than zero.  They can get less than zero due to rounding errors
        when accumulating. */
    void clip(int bucket)
    {
        dist[bucket] = std::max(dist[bucket], 0.0);
        sqr[bucket] = std::max(sqr[bucket], 0.0);
        wt[bucket] = std::max(wt[bucket], 0.0);
    }

    /** Swap the weight between the given two buckets. */
    void swap_buckets(int b1, int b2)
    {
        std::swap(dist[b1], dist[b2]);
        std::swap(sqr[b1],  sqr[b2]);
        std::swap(wt[b1],   wt[b2]);
    }

    double dist[3];  ///< Sum of value * weight
    double sqr[3];   ///< Sum of weight * value^2
    double wt[3];    ///< Sum of weight
};


/*****************************************************************************/
/* Z FORMULA                                                                 */
/*****************************************************************************/

/** Return the Z score.  This is defined as
    \f[
        Z = 2 \sum_{j \in \{0,1\}} \sum_{l \in \mathcal{y}}
        \sqrt{W^{jl}_{+1} W^{jl}_{-1} }
    \f]

    The lower the Z score, the better the fit.
*/

/* Function object to calculate Z for the non-trueonly case. */
struct Z_regress {
    static const double worst   = 1e100;  // worst possible Z value
    static const double none    = -1.0;  // flag to indicate couldn't calculate
    static const double perfect = 0.0;  // best possible Z value

    static bool equal(double z1, double z2)
    {
        return z1 == z2;
    }

    static bool better(double z1, double z2)
    {
        return z1 != none && z1 < z2;
    }

    /* Return the constant missing part. */
    template<class W>
    double missing(const W & w, bool optional) const
    {
        if (w.wt[MISSING] > 1e-20)
            return w.sqr[MISSING]
                - (w.dist[MISSING] * w.dist[MISSING]) / w.wt[MISSING];
        else return 0.0;
    }

    /* Return the non-missing part.  This calculates the variance from the
       W. */
    template<class W>
    double non_missing(const W & w, double missing) const
    {
        double result = missing;
        for (unsigned i = 0;  i < 2;  ++i) {
            if (w.wt[i] > 1e-20)
                result += w.sqr[i] - (w.dist[i] * w.dist[i]) / w.wt[i];
        }
        return result;
    }

    template<class W>
    double non_missing_presence(const W & w, double missing) const
    {
        return non_missing(w, missing);
    }

    template<class W>
    double operator () (const W & w) const
    {
        return non_missing(w, missing(w));
    }
    
    /** Return true if it is possible for us to beat the Z score already
        given. */
    template<class W>
    bool can_beat(const W & w, double missing, double z_best) const
    {
        return missing <= (z_best * 1.0001);
    }
};


/*****************************************************************************/
/* C FORMULA                                                                 */
/*****************************************************************************/

/** Return the C scores.  The vector returned will have 3 rows, which each
    contain one value: the prediction for true/false/missing.

    These are simply the means of the weights in the category.
*/

struct C_regress {

    /* Calculate the means for each weight in each category. */
    template<class W>
    std::vector<distribution<float> >
    operator() (const W & w, float epsilon, bool optional) const
    {
        distribution<float> model(1, 0.0);
        std::vector<distribution<float> > result(3, model);

        for (unsigned i = 0;  i < 3;  ++i) {
            if (w.wt[i] > 1e-20)
                result[i][0] = w.dist[i] / w.wt[i];
            else {
                /* No weight, so take the sample mean. */
                double total = 0.0, total_wt = 0.0;
                for (unsigned j = 0;  j < 3;  ++j) {
                    total += w.dist[j];
                    total_wt += w.wt[j];
                }
                result[i][0] = total / total_wt;
            }
        }
        return result;
    }

    Stump::Update update_alg() const { return Stump::NORMAL; }
};


} // namespace ML



#endif /* __boosting__stump_regress_h__ */

