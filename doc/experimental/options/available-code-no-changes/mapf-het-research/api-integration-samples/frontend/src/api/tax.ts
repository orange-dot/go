import { apiGet, apiPost } from './client'
import type { Money } from './transactions'

export interface TaxCalculationResult {
  transactionId: string
  taxableAmount: Money
  taxAmount: Money
  taxRate: number
  taxRatePercentage: number
  jurisdiction: string
  taxType: string
  periodId: string
  isExempt: boolean
  exemptionReason?: string
}

export interface TaxReport {
  reportId: string
  periodId: string
  jurisdiction: string
  year: number
  quarter: number
  totalTaxable: Money
  totalTax: Money
  transactionCount: number
  calculations: TaxCalculation[]
  thresholdAlerts: ThresholdAlert[]
  generatedAt: string
  generatedBy: string
}

export interface TaxCalculation {
  transactionId: string
  taxableAmount: number
  taxAmount: number
  rate: number
  calculatedAt: string
}

export interface ThresholdAlert {
  type: string
  currentValue: number
  thresholdValue: number
  exceededAt: string
}

export interface TaxLiability {
  jurisdiction: string
  year: number
  quarters: QuarterlyTaxSummary[]
  yearlyTotals: {
    totalTaxable: Money
    totalTax: Money
    totalPaid: Money
    outstanding: Money
  }
  totalTransactions: number
  allReportsGenerated: boolean
  alerts: { type: string; message: string; timestamp: string }[]
}

export interface QuarterlyTaxSummary {
  quarter: number
  periodId: string
  totalTaxable: Money
  totalTax: Money
  paymentsMade?: Money
  outstandingTax: Money
  transactionCount: number
  reportGenerated: boolean
  reportGeneratedAt?: string
}

export interface TaxRate {
  rate: number
  percentage: number
  jurisdiction: string
  taxType: string
  category?: string
  effectiveFrom: string
  effectiveTo?: string
}

export const taxApi = {
  calculate: (input: {
    transactionId: string
    amount: number
    currency?: string
    jurisdiction: string
    category?: string
  }) => apiPost<TaxCalculationResult>('/tax/calculate', input),

  generateReport: (input: {
    jurisdiction: string
    year?: number
    quarter?: number
    generatedBy?: string
  }) => apiPost<TaxReport>('/tax/report', input),

  getLiability: (jurisdiction: string, year: number) =>
    apiGet<TaxLiability>(`/tax/liability/${jurisdiction}/${year}`),

  getPeriodDetail: (periodId: string) =>
    apiGet<TaxReport>(`/tax/period/${periodId}`),

  getRates: (jurisdiction: string) =>
    apiGet<TaxRate[]>(`/tax/rates/${jurisdiction}`),
}
