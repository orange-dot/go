import { Bell, RefreshCw, Settings } from 'lucide-react'
import { useQueryClient } from '@tanstack/react-query'
import { useEventStats } from '@/hooks/useEventStream'

export function Header() {
  const queryClient = useQueryClient()
  const { data: stats } = useEventStats()

  const totalEvents = stats?.stats
    ? Object.values(stats.stats).reduce((sum, s) => sum + s.totalEvents, 0)
    : 0

  const handleRefresh = () => {
    queryClient.invalidateQueries()
  }

  return (
    <header className="h-16 bg-white border-b border-gray-200 px-6 flex items-center justify-between">
      <div className="flex items-center gap-4">
        <h2 className="text-lg font-semibold text-gray-900">
          Event-Driven Finance Platform
        </h2>
        <span className="px-2 py-1 text-xs font-medium bg-green-100 text-green-800 rounded-full">
          {totalEvents} events
        </span>
      </div>

      <div className="flex items-center gap-2">
        <button
          onClick={handleRefresh}
          className="p-2 text-gray-500 hover:text-gray-700 hover:bg-gray-100 rounded-lg transition-colors"
          title="Refresh data"
        >
          <RefreshCw className="h-5 w-5" />
        </button>

        <button
          className="p-2 text-gray-500 hover:text-gray-700 hover:bg-gray-100 rounded-lg transition-colors relative"
          title="Notifications"
        >
          <Bell className="h-5 w-5" />
          <span className="absolute top-1 right-1 h-2 w-2 bg-red-500 rounded-full" />
        </button>

        <button
          className="p-2 text-gray-500 hover:text-gray-700 hover:bg-gray-100 rounded-lg transition-colors"
          title="Settings"
        >
          <Settings className="h-5 w-5" />
        </button>
      </div>
    </header>
  )
}
