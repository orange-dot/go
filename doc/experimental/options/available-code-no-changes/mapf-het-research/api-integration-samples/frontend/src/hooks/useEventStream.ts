import { useQuery } from '@tanstack/react-query'
import { apiGet } from '@/api/client'

export interface StreamEvent {
  eventId: string
  streamName: string
  eventType: string
  timestamp: string
  position: number
  data: Record<string, unknown>
}

export interface EventsResponse {
  events: StreamEvent[]
  count: number
  prefix: string
}

export function useEventStream(prefix: string = '', refetchInterval: number = 5000) {
  return useQuery({
    queryKey: ['events', 'recent', prefix],
    queryFn: () => apiGet<EventsResponse>(`/events/recent?prefix=${prefix}&limit=50`),
    refetchInterval,
  })
}

export interface EventStats {
  stats: Record<string, {
    streamCount: number
    totalEvents: number
    eventsByType: Record<string, number>
    firstEventAt?: string
    lastEventAt?: string
  }>
  generatedAt: string
}

export function useEventStats() {
  return useQuery({
    queryKey: ['events', 'stats'],
    queryFn: () => apiGet<EventStats>('/events/stats'),
    refetchInterval: 10000,
  })
}
