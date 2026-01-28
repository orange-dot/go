import { createFileRoute } from '@tanstack/react-router'
import { TransactionsPage } from '@/pages/TransactionsPage'

export const Route = createFileRoute('/transactions')({
  component: TransactionsPage,
})
