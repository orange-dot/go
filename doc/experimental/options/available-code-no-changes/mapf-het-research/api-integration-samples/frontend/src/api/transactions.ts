import { apiGet, apiPost } from './client'

export interface Money {
  value: number
  currency: string
}

export interface Transaction {
  transactionId: string
  amount: Money
  description: string
  category?: string
  status: 'Pending' | 'Approved' | 'Rejected' | 'Voided'
  createdAt: string
  tax?: {
    taxAmount: number
    taxRate: number
    periodId: string
  }
}

export interface CreateTransactionInput {
  amount: number
  currency: string
  description: string
  category?: string
  sourceSystem?: string
  reference?: string
  createdBy?: string
  jurisdiction?: string
}

export const transactionsApi = {
  create: (input: CreateTransactionInput) =>
    apiPost<Transaction>('/transactions', input),

  approve: (id: string, approvedBy?: string, notes?: string) =>
    apiPost<Transaction>(`/transactions/${id}/approve`, { approvedBy, notes }),

  reject: (id: string, rejectedBy: string, reason: string) =>
    apiPost<Transaction>(`/transactions/${id}/reject`, { rejectedBy, reason }),

  getRecent: () =>
    apiGet<{ events: TransactionEvent[] }>('/events/recent?prefix=transaction-'),
}

export interface TransactionEvent {
  eventId: string
  streamName: string
  eventType: string
  timestamp: string
  data: {
    transactionId: string
    amount?: Money
    description?: string
    category?: string
    approvedBy?: string
    rejectedBy?: string
    reason?: string
  }
}
