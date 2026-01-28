import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { format } from 'date-fns'
import { Search, Filter, ChevronDown, ChevronRight } from 'lucide-react'
import { apiGet } from '@/api/client'

interface AuditEntry {
  eventId: string
  streamName: string
  eventType: string
  timestamp: string
  userId?: string
  correlationId?: string
  data: Record<string, unknown>
  version: number
}

interface AuditTrailResponse {
  entries: AuditEntry[]
  totalCount: number
  query: {
    streamPrefix?: string
    eventType?: string
    fromDate?: string
    toDate?: string
    maxResults: number
  }
}

const EVENT_TYPES = [
  'TransactionCreated',
  'TransactionApproved',
  'TransactionRejected',
  'ReconciliationStarted',
  'TransactionsMatched',
  'TaxCalculated',
  'LedgerEntryPosted',
]

export function AuditTrailPage() {
  const [streamPrefix, setStreamPrefix] = useState('')
  const [eventType, setEventType] = useState('')
  const [expandedRows, setExpandedRows] = useState<Set<string>>(new Set())

  const { data, isLoading } = useQuery({
    queryKey: ['audit-trail', streamPrefix, eventType],
    queryFn: () => {
      const params = new URLSearchParams()
      if (streamPrefix) params.set('streamPrefix', streamPrefix)
      if (eventType) params.set('eventType', eventType)
      params.set('limit', '100')
      return apiGet<AuditTrailResponse>(`/audit-trail?${params}`)
    },
  })

  const toggleRow = (eventId: string) => {
    setExpandedRows((prev) => {
      const next = new Set(prev)
      if (next.has(eventId)) {
        next.delete(eventId)
      } else {
        next.add(eventId)
      }
      return next
    })
  }

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-bold text-gray-900">Audit Trail</h1>
        <p className="text-gray-500">Complete immutable event history</p>
      </div>

      {/* Filters */}
      <div className="bg-white rounded-lg border border-gray-200 p-4">
        <div className="flex items-center gap-4">
          <div className="flex-1">
            <label className="block text-xs text-gray-500 mb-1">Stream Prefix</label>
            <div className="relative">
              <Search className="absolute left-3 top-1/2 -translate-y-1/2 h-4 w-4 text-gray-400" />
              <input
                type="text"
                value={streamPrefix}
                onChange={(e) => setStreamPrefix(e.target.value)}
                placeholder="e.g., transaction-, ledger-, tax-"
                className="w-full pl-10 pr-4 py-2 border border-gray-300 rounded-lg focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>

          <div className="w-64">
            <label className="block text-xs text-gray-500 mb-1">Event Type</label>
            <div className="relative">
              <Filter className="absolute left-3 top-1/2 -translate-y-1/2 h-4 w-4 text-gray-400" />
              <select
                value={eventType}
                onChange={(e) => setEventType(e.target.value)}
                className="w-full pl-10 pr-4 py-2 border border-gray-300 rounded-lg focus:ring-2 focus:ring-blue-500 appearance-none"
              >
                <option value="">All events</option>
                {EVENT_TYPES.map((type) => (
                  <option key={type} value={type}>{type}</option>
                ))}
              </select>
            </div>
          </div>
        </div>
      </div>

      {/* Results */}
      <div className="bg-white rounded-lg border border-gray-200">
        <div className="px-4 py-3 border-b border-gray-200">
          <p className="text-sm text-gray-500">
            Showing {data?.entries.length ?? 0} of {data?.totalCount ?? 0} events
          </p>
        </div>

        {isLoading ? (
          <div className="p-4 space-y-3">
            {Array.from({ length: 10 }).map((_, i) => (
              <div key={i} className="animate-pulse flex gap-4">
                <div className="h-10 w-10 bg-gray-200 rounded" />
                <div className="flex-1 space-y-2">
                  <div className="h-4 bg-gray-200 rounded w-1/3" />
                  <div className="h-3 bg-gray-100 rounded w-1/2" />
                </div>
              </div>
            ))}
          </div>
        ) : data?.entries && data.entries.length > 0 ? (
          <div className="divide-y divide-gray-100">
            {data.entries.map((entry) => {
              const isExpanded = expandedRows.has(entry.eventId)
              return (
                <div key={entry.eventId} className="hover:bg-gray-50">
                  <button
                    onClick={() => toggleRow(entry.eventId)}
                    className="w-full px-4 py-3 flex items-start gap-3 text-left"
                  >
                    <div className="pt-1">
                      {isExpanded ? (
                        <ChevronDown className="h-4 w-4 text-gray-400" />
                      ) : (
                        <ChevronRight className="h-4 w-4 text-gray-400" />
                      )}
                    </div>

                    <div className="flex-1 min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="font-medium text-gray-900">
                          {entry.eventType}
                        </span>
                        <span className="text-xs text-gray-400">
                          v{entry.version}
                        </span>
                      </div>
                      <p className="text-sm text-gray-500 truncate">
                        {entry.streamName}
                      </p>
                    </div>

                    <div className="text-right">
                      <p className="text-sm text-gray-900">
                        {format(new Date(entry.timestamp), 'MMM d, yyyy')}
                      </p>
                      <p className="text-xs text-gray-500">
                        {format(new Date(entry.timestamp), 'HH:mm:ss')}
                      </p>
                    </div>
                  </button>

                  {isExpanded && (
                    <div className="px-4 pb-4 ml-7">
                      <div className="bg-gray-50 rounded-lg p-4 space-y-3">
                        <div className="grid grid-cols-2 gap-4 text-sm">
                          <div>
                            <span className="text-gray-500">Event ID:</span>
                            <code className="ml-2 text-xs bg-gray-200 px-1 rounded">
                              {entry.eventId}
                            </code>
                          </div>
                          {entry.correlationId && (
                            <div>
                              <span className="text-gray-500">Correlation ID:</span>
                              <code className="ml-2 text-xs bg-gray-200 px-1 rounded">
                                {entry.correlationId}
                              </code>
                            </div>
                          )}
                          {entry.userId && (
                            <div>
                              <span className="text-gray-500">User:</span>
                              <span className="ml-2">{entry.userId}</span>
                            </div>
                          )}
                        </div>

                        <div>
                          <p className="text-sm text-gray-500 mb-1">Event Data:</p>
                          <pre className="text-xs bg-gray-900 text-gray-100 p-3 rounded-lg overflow-x-auto">
                            {JSON.stringify(entry.data, null, 2)}
                          </pre>
                        </div>
                      </div>
                    </div>
                  )}
                </div>
              )
            })}
          </div>
        ) : (
          <div className="p-8 text-center text-gray-500">
            No events found. Create some transactions to see audit trail entries.
          </div>
        )}
      </div>

      {/* Info */}
      <div className="bg-blue-50 border border-blue-200 rounded-lg p-4">
        <h3 className="font-medium text-blue-900 mb-2">About the Audit Trail</h3>
        <ul className="text-sm text-blue-800 space-y-1">
          <li>All events are stored in EventStoreDB and are immutable</li>
          <li>Events can be used to reconstruct state at any point in time</li>
          <li>Each event has a version number for optimistic concurrency</li>
          <li>Correlation IDs link related events across streams</li>
        </ul>
      </div>
    </div>
  )
}
