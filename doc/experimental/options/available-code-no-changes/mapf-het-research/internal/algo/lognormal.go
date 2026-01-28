// Package algo - LogNormal distribution utilities for stochastic planning.
package algo

import (
	"math"
	"math/rand"
)

// LogNormalDist represents a LogNormal distribution.
// If X ~ LogNormal(μ, σ), then ln(X) ~ Normal(μ, σ).
type LogNormalDist struct {
	Mu    float64 // Location parameter (mean of ln(X))
	Sigma float64 // Scale parameter (std dev of ln(X))
}

// NewLogNormalFromMeanStd creates a LogNormal from mean and std of X (not ln(X)).
func NewLogNormalFromMeanStd(mean, std float64) LogNormalDist {
	if mean <= 0 || std < 0 {
		return LogNormalDist{Mu: 0, Sigma: 0}
	}

	// Derive μ and σ from E[X] and Var[X]
	// E[X] = exp(μ + σ²/2)
	// Var[X] = exp(2μ + σ²)(exp(σ²) - 1)
	variance := std * std
	sigma2 := math.Log(1 + variance/(mean*mean))
	sigma := math.Sqrt(sigma2)
	mu := math.Log(mean) - sigma2/2

	return LogNormalDist{Mu: mu, Sigma: sigma}
}

// Mean returns E[X] for X ~ LogNormal(μ, σ).
func (d LogNormalDist) Mean() float64 {
	return math.Exp(d.Mu + d.Sigma*d.Sigma/2)
}

// Variance returns Var[X].
func (d LogNormalDist) Variance() float64 {
	sigma2 := d.Sigma * d.Sigma
	return math.Exp(2*d.Mu+sigma2) * (math.Exp(sigma2) - 1)
}

// Std returns standard deviation.
func (d LogNormalDist) Std() float64 {
	return math.Sqrt(d.Variance())
}

// Median returns the median of the distribution.
func (d LogNormalDist) Median() float64 {
	return math.Exp(d.Mu)
}

// Mode returns the mode (most likely value).
func (d LogNormalDist) Mode() float64 {
	return math.Exp(d.Mu - d.Sigma*d.Sigma)
}

// Sample generates a random sample from the distribution.
func (d LogNormalDist) Sample(rng *rand.Rand) float64 {
	// Generate Normal(μ, σ) then exponentiate
	normal := rng.NormFloat64()*d.Sigma + d.Mu
	return math.Exp(normal)
}

// PDF returns the probability density at x.
func (d LogNormalDist) PDF(x float64) float64 {
	if x <= 0 {
		return 0
	}

	lnX := math.Log(x)
	z := (lnX - d.Mu) / d.Sigma

	return math.Exp(-z*z/2) / (x * d.Sigma * math.Sqrt(2*math.Pi))
}

// CDF returns P(X <= x).
func (d LogNormalDist) CDF(x float64) float64 {
	if x <= 0 {
		return 0
	}

	z := (math.Log(x) - d.Mu) / d.Sigma
	return normalCDF(z)
}

// Quantile returns x such that P(X <= x) = p.
func (d LogNormalDist) Quantile(p float64) float64 {
	if p <= 0 {
		return 0
	}
	if p >= 1 {
		return math.Inf(1)
	}

	z := normalQuantile(p)
	return math.Exp(d.Mu + d.Sigma*z)
}

// normalCDF computes the standard normal CDF using the error function.
func normalCDF(z float64) float64 {
	return 0.5 * (1 + math.Erf(z/math.Sqrt(2)))
}

// normalQuantile computes the inverse standard normal CDF (probit function).
// Uses Abramowitz and Stegun approximation.
func normalQuantile(p float64) float64 {
	if p <= 0 {
		return math.Inf(-1)
	}
	if p >= 1 {
		return math.Inf(1)
	}
	if p == 0.5 {
		return 0
	}

	// Rational approximation for lower region
	if p < 0.5 {
		return -rationalApproxForNormalQuantile(math.Sqrt(-2 * math.Log(p)))
	}
	return rationalApproxForNormalQuantile(math.Sqrt(-2 * math.Log(1-p)))
}

func rationalApproxForNormalQuantile(t float64) float64 {
	// Coefficients from Abramowitz and Stegun
	c := []float64{2.515517, 0.802853, 0.010328}
	d := []float64{1.432788, 0.189269, 0.001308}

	return t - (c[0]+c[1]*t+c[2]*t*t)/(1+d[0]*t+d[1]*t*t+d[2]*t*t*t)
}

// FentonWilkinson approximates max/sum of LogNormals as a LogNormal.
// Uses the Fenton-Wilkinson method for sums, adapted for max.
func FentonWilkinson(dists []LogNormalDist) LogNormalDist {
	if len(dists) == 0 {
		return LogNormalDist{}
	}
	if len(dists) == 1 {
		return dists[0]
	}

	// For sum of LogNormals:
	// μ_sum ≈ log(Σ exp(μ_i + σ_i²/2)) - σ_sum²/2
	// σ_sum² ≈ log(1 + Σ exp(2μ_i + σ_i²)(exp(σ_i²) - 1) / (Σ exp(μ_i + σ_i²/2))²)

	var sumMean float64
	var sumVar float64

	for _, d := range dists {
		sigma2 := d.Sigma * d.Sigma
		meanTerm := math.Exp(d.Mu + sigma2/2)
		varTerm := math.Exp(2*d.Mu+sigma2) * (math.Exp(sigma2) - 1)

		sumMean += meanTerm
		sumVar += varTerm
	}

	if sumMean <= 0 {
		return LogNormalDist{}
	}

	sigmaSum2 := math.Log(1 + sumVar/(sumMean*sumMean))
	muSum := math.Log(sumMean) - sigmaSum2/2

	return LogNormalDist{Mu: muSum, Sigma: math.Sqrt(sigmaSum2)}
}

// MaxApproximation approximates max of independent LogNormals.
// Uses order statistics approximation.
func MaxApproximation(dists []LogNormalDist) LogNormalDist {
	if len(dists) == 0 {
		return LogNormalDist{}
	}
	if len(dists) == 1 {
		return dists[0]
	}

	// For max of n iid LogNormals, use extreme value approximation
	// For non-iid, we use moment matching

	n := float64(len(dists))

	// Compute combined mean and variance via simulation approximation
	// E[max] ≈ μ_max + σ_max * Φ^{-1}((n-0.375)/(n+0.25))

	// Use the largest mean distribution as base, adjust
	var maxMean float64
	var maxIdx int
	for i, d := range dists {
		m := d.Mean()
		if m > maxMean {
			maxMean = m
			maxIdx = i
		}
	}

	base := dists[maxIdx]

	// Adjust sigma based on number of competing distributions
	scaleFactor := 1.0 + 0.2*math.Log(n)
	adjustedSigma := base.Sigma * scaleFactor

	// Adjust mu to account for max being higher than individual mean
	quantileFactor := normalQuantile((n - 0.375) / (n + 0.25))
	adjustedMu := base.Mu + base.Sigma*quantileFactor*0.5

	return LogNormalDist{Mu: adjustedMu, Sigma: adjustedSigma}
}

// ConvolveDurations combines travel time with task duration distributions.
func ConvolveDurations(travel, task LogNormalDist) LogNormalDist {
	// Sum of independent LogNormals approximated via Fenton-Wilkinson
	return FentonWilkinson([]LogNormalDist{travel, task})
}

// ScaleLogNormal scales a LogNormal by a constant factor.
// If X ~ LogNormal(μ, σ), then cX ~ LogNormal(μ + ln(c), σ).
func ScaleLogNormal(d LogNormalDist, c float64) LogNormalDist {
	if c <= 0 {
		return LogNormalDist{}
	}
	return LogNormalDist{
		Mu:    d.Mu + math.Log(c),
		Sigma: d.Sigma,
	}
}
