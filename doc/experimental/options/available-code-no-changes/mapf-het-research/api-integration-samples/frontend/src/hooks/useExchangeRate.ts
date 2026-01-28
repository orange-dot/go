import { useMemo } from 'react'

// Fallback rates for display purposes
const FALLBACK_RATES: Record<string, number> = {
  'EUR_USD': 1.08,
  'EUR_GBP': 0.86,
  'EUR_RSD': 117.0,
  'EUR_CHF': 0.96,
  'USD_EUR': 0.93,
  'USD_GBP': 0.80,
  'GBP_EUR': 1.16,
  'GBP_USD': 1.25,
  'RSD_EUR': 0.0085,
}

export function useExchangeRate(from: string, to: string): number {
  return useMemo(() => {
    if (from === to) return 1

    const directKey = `${from}_${to}`
    if (FALLBACK_RATES[directKey]) {
      return FALLBACK_RATES[directKey]
    }

    const reverseKey = `${to}_${from}`
    if (FALLBACK_RATES[reverseKey]) {
      return 1 / FALLBACK_RATES[reverseKey]
    }

    // Try through EUR
    const fromEurKey = `${from}_EUR`
    const eurToKey = `EUR_${to}`
    if (FALLBACK_RATES[fromEurKey] && FALLBACK_RATES[eurToKey]) {
      return FALLBACK_RATES[fromEurKey] * FALLBACK_RATES[eurToKey]
    }

    return 1
  }, [from, to])
}

export function formatCurrency(amount: number, currency: string): string {
  return new Intl.NumberFormat('en-US', {
    style: 'currency',
    currency,
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  }).format(amount)
}

export function convertAmount(
  amount: number,
  fromCurrency: string,
  toCurrency: string
): number {
  if (fromCurrency === toCurrency) return amount

  const directKey = `${fromCurrency}_${toCurrency}`
  if (FALLBACK_RATES[directKey]) {
    return amount * FALLBACK_RATES[directKey]
  }

  const reverseKey = `${toCurrency}_${fromCurrency}`
  if (FALLBACK_RATES[reverseKey]) {
    return amount / FALLBACK_RATES[reverseKey]
  }

  return amount
}
