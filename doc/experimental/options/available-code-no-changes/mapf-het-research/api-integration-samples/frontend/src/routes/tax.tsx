import { createFileRoute } from '@tanstack/react-router'
import { TaxReportPage } from '@/pages/TaxReportPage'

export const Route = createFileRoute('/tax')({
  component: TaxReportPage,
})
