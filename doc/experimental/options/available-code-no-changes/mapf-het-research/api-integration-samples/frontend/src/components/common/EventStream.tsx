import { useEventStream, type StreamEvent } from '@/hooks/useEventStream'
import { format } from 'date-fns'
import clsx from 'clsx'
import { Activity, CheckCircle, XCircle, Clock, ArrowRightLeft } from 'lucide-react'

interface EventStreamProps {
  prefix?: string
  maxItems?: number
  className?: string
}

const eventTypeConfig: Record<string, { icon: typeof Activity; color: string }> = {
  TransactionCreated: { icon: ArrowRightLeft, color: 'text-blue-500' },
  TransactionApproved: { icon: CheckCircle, color: 'text-green-500' },
  TransactionRejected: { icon: XCircle, color: 'text-red-500' },
  ReconciliationStarted: { icon: Clock, color: 'text-yellow-500' },
  TransactionsMatched: { icon: CheckCircle, color: 'text-green-500' },
  TaxCalculated: { icon: Activity, color: 'text-purple-500' },
  LedgerEntryPosted: { icon: Activity, color: 'text-indigo-500' },
}

export function EventStream({ prefix = '', maxItems = 10, className }: EventStreamProps) {
  const { data, isLoading, error } = useEventStream(prefix)

  if (isLoading) {
    return (
      <div className={clsx('animate-pulse', className)}>
        {Array.from({ length: 5 }).map((_, i) => (
          <div key={i} className="flex gap-3 py-3 border-b border-gray-100">
            <div className="h-8 w-8 bg-gray-200 rounded-full" />
            <div className="flex-1 space-y-2">
              <div className="h-4 bg-gray-200 rounded w-1/3" />
              <div className="h-3 bg-gray-100 rounded w-1/2" />
            </div>
          </div>
        ))}
      </div>
    )
  }

  if (error) {
    return (
      <div className={clsx('text-red-500 text-sm', className)}>
        Error loading events: {error.message}
      </div>
    )
  }

  const events = data?.events.slice(0, maxItems) || []

  if (events.length === 0) {
    return (
      <div className={clsx('text-gray-500 text-sm text-center py-8', className)}>
        No events yet. Create a transaction to see events appear here.
      </div>
    )
  }

  return (
    <div className={className}>
      {events.map((event) => (
        <EventItem key={event.eventId} event={event} />
      ))}
    </div>
  )
}

function EventItem({ event }: { event: StreamEvent }) {
  const config = eventTypeConfig[event.eventType] || {
    icon: Activity,
    color: 'text-gray-500',
  }
  const Icon = config.icon

  return (
    <div className="flex gap-3 py-3 border-b border-gray-100 last:border-0">
      <div className={clsx('h-8 w-8 rounded-full flex items-center justify-center bg-gray-100', config.color)}>
        <Icon className="h-4 w-4" />
      </div>
      <div className="flex-1 min-w-0">
        <div className="flex items-center gap-2">
          <span className="font-medium text-sm text-gray-900">{event.eventType}</span>
          <span className="text-xs text-gray-400">
            {format(new Date(event.timestamp), 'HH:mm:ss')}
          </span>
        </div>
        <p className="text-xs text-gray-500 truncate">
          {event.streamName}
        </p>
        {event.data && (
          <div className="mt-1 text-xs text-gray-400">
            {formatEventData(event.data)}
          </div>
        )}
      </div>
    </div>
  )
}

function formatEventData(data: Record<string, unknown>): string {
  if (data.amount && typeof data.amount === 'object') {
    const amount = data.amount as { value?: number; amount?: number; currency?: string }
    const value = amount.value ?? amount.amount
    const currency = amount.currency ?? 'EUR'
    return `${value} ${currency}`
  }
  if (data.description) {
    return String(data.description).substring(0, 50)
  }
  if (data.transactionId) {
    return `Transaction: ${String(data.transactionId).substring(0, 8)}...`
  }
  return ''
}
