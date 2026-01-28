import { createFileRoute } from '@tanstack/react-router'
import { LedgerPage } from '@/pages/LedgerPage'

export const Route = createFileRoute('/ledger')({
  component: LedgerPage,
})
