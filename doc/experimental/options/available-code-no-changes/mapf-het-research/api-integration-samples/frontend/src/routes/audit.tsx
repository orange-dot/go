import { createFileRoute } from '@tanstack/react-router'
import { AuditTrailPage } from '@/pages/AuditTrailPage'

export const Route = createFileRoute('/audit')({
  component: AuditTrailPage,
})
