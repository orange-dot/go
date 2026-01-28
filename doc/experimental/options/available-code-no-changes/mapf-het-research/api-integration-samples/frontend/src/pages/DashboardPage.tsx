import { useQuery } from '@tanstack/react-query'
import { BarChart, Bar, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts'
import { Activity, ArrowLeftRight, Calculator, GitCompare } from 'lucide-react'
import { ledgerApi } from '@/api/ledger'
import { useEventStats } from '@/hooks/useEventStream'
import { EventStream } from '@/components/common/EventStream'
import { BalanceCard } from '@/components/common/MoneyDisplay'

export function DashboardPage() {
  const { data: ledgerData, isLoading: ledgerLoading } = useQuery({
    queryKey: ['ledger', 'balance'],
    queryFn: () => ledgerApi.getBalance('EUR'),
  })

  const { data: eventStats } = useEventStats()

  const statsCards = [
    {
      title: 'Total Transactions',
      value: eventStats?.stats?.transaction?.totalEvents ?? 0,
      icon: ArrowLeftRight,
      color: 'text-blue-600',
      bg: 'bg-blue-100',
    },
    {
      title: 'Ledger Entries',
      value: eventStats?.stats?.ledger?.totalEvents ?? 0,
      icon: Activity,
      color: 'text-green-600',
      bg: 'bg-green-100',
    },
    {
      title: 'Tax Events',
      value: eventStats?.stats?.tax?.totalEvents ?? 0,
      icon: Calculator,
      color: 'text-purple-600',
      bg: 'bg-purple-100',
    },
    {
      title: 'Reconciliations',
      value: eventStats?.stats?.reconciliation?.totalEvents ?? 0,
      icon: GitCompare,
      color: 'text-orange-600',
      bg: 'bg-orange-100',
    },
  ]

  const chartData = eventStats?.stats
    ? Object.entries(eventStats.stats).map(([name, data]) => ({
        name: name.charAt(0).toUpperCase() + name.slice(1),
        events: data.totalEvents,
        streams: data.streamCount,
      }))
    : []

  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-bold text-gray-900">Dashboard</h1>
        <p className="text-gray-500">Event-driven finance platform overview</p>
      </div>

      {/* Stats Cards */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        {statsCards.map((stat) => (
          <div
            key={stat.title}
            className="bg-white rounded-lg border border-gray-200 p-4"
          >
            <div className="flex items-center justify-between">
              <div>
                <p className="text-sm font-medium text-gray-500">{stat.title}</p>
                <p className="text-2xl font-bold text-gray-900 mt-1">
                  {stat.value.toLocaleString()}
                </p>
              </div>
              <div className={`p-3 rounded-lg ${stat.bg}`}>
                <stat.icon className={`h-6 w-6 ${stat.color}`} />
              </div>
            </div>
          </div>
        ))}
      </div>

      {/* Main Content */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Chart */}
        <div className="lg:col-span-2 bg-white rounded-lg border border-gray-200 p-6">
          <h2 className="text-lg font-semibold text-gray-900 mb-4">
            Event Distribution
          </h2>
          {chartData.length > 0 ? (
            <ResponsiveContainer width="100%" height={300}>
              <BarChart data={chartData}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="name" />
                <YAxis />
                <Tooltip />
                <Bar dataKey="events" fill="#3b82f6" name="Events" />
                <Bar dataKey="streams" fill="#10b981" name="Streams" />
              </BarChart>
            </ResponsiveContainer>
          ) : (
            <div className="h-[300px] flex items-center justify-center text-gray-400">
              No event data available
            </div>
          )}
        </div>

        {/* Event Stream */}
        <div className="bg-white rounded-lg border border-gray-200 p-6">
          <h2 className="text-lg font-semibold text-gray-900 mb-4">
            Live Events
          </h2>
          <EventStream maxItems={8} />
        </div>
      </div>

      {/* Balances */}
      <div className="bg-white rounded-lg border border-gray-200 p-6">
        <h2 className="text-lg font-semibold text-gray-900 mb-4">
          Ledger Balances
        </h2>
        {ledgerLoading ? (
          <div className="animate-pulse grid grid-cols-4 gap-4">
            {Array.from({ length: 4 }).map((_, i) => (
              <div key={i} className="h-20 bg-gray-100 rounded-lg" />
            ))}
          </div>
        ) : ledgerData?.consolidatedBalances ? (
          <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
            {Object.entries(ledgerData.consolidatedBalances).map(([account, balance]) => (
              <BalanceCard
                key={account}
                title={account}
                amount={{ value: balance, currency: 'EUR' }}
              />
            ))}
          </div>
        ) : (
          <p className="text-gray-500">No balance data available</p>
        )}
      </div>
    </div>
  )
}
