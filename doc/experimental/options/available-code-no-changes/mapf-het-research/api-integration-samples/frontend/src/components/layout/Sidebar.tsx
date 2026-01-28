import { Link, useLocation } from '@tanstack/react-router'
import {
  LayoutDashboard,
  ArrowLeftRight,
  BookOpen,
  Calculator,
  GitCompare,
  ClipboardList,
  Database,
} from 'lucide-react'
import clsx from 'clsx'

const navigation = [
  { name: 'Dashboard', href: '/', icon: LayoutDashboard },
  { name: 'Transactions', href: '/transactions', icon: ArrowLeftRight },
  { name: 'Ledger', href: '/ledger', icon: BookOpen },
  { name: 'Tax Reports', href: '/tax', icon: Calculator },
  { name: 'Reconciliation', href: '/reconciliation', icon: GitCompare },
  { name: 'Audit Trail', href: '/audit', icon: ClipboardList },
]

export function Sidebar() {
  const location = useLocation()

  return (
    <div className="flex flex-col w-64 bg-gray-900 text-white">
      <div className="flex items-center gap-2 h-16 px-4 border-b border-gray-800">
        <Database className="h-8 w-8 text-blue-400" />
        <div>
          <h1 className="text-lg font-semibold">Finance Platform</h1>
          <p className="text-xs text-gray-400">Event-Driven Demo</p>
        </div>
      </div>

      <nav className="flex-1 px-2 py-4 space-y-1">
        {navigation.map((item) => {
          const isActive = location.pathname === item.href
          return (
            <Link
              key={item.name}
              to={item.href}
              className={clsx(
                'flex items-center gap-3 px-3 py-2 rounded-lg text-sm font-medium transition-colors',
                isActive
                  ? 'bg-blue-600 text-white'
                  : 'text-gray-300 hover:bg-gray-800 hover:text-white'
              )}
            >
              <item.icon className="h-5 w-5" />
              {item.name}
            </Link>
          )
        })}
      </nav>

      <div className="px-4 py-4 border-t border-gray-800">
        <div className="text-xs text-gray-500">
          <p>EventStoreDB</p>
          <a
            href="http://localhost:2113"
            target="_blank"
            rel="noopener noreferrer"
            className="text-blue-400 hover:underline"
          >
            localhost:2113
          </a>
        </div>
      </div>
    </div>
  )
}
