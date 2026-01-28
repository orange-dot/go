import clsx from 'clsx'

type Status =
  | 'Pending'
  | 'Approved'
  | 'Rejected'
  | 'Voided'
  | 'InProgress'
  | 'Completed'
  | 'Failed'
  | 'Exact'
  | 'Fuzzy'
  | 'Manual'

interface StatusBadgeProps {
  status: Status | string
  size?: 'sm' | 'md'
}

const statusConfig: Record<string, { bg: string; text: string }> = {
  Pending: { bg: 'bg-yellow-100', text: 'text-yellow-800' },
  Approved: { bg: 'bg-green-100', text: 'text-green-800' },
  Rejected: { bg: 'bg-red-100', text: 'text-red-800' },
  Voided: { bg: 'bg-gray-100', text: 'text-gray-800' },
  InProgress: { bg: 'bg-blue-100', text: 'text-blue-800' },
  Completed: { bg: 'bg-green-100', text: 'text-green-800' },
  Failed: { bg: 'bg-red-100', text: 'text-red-800' },
  Exact: { bg: 'bg-green-100', text: 'text-green-800' },
  Fuzzy: { bg: 'bg-yellow-100', text: 'text-yellow-800' },
  Manual: { bg: 'bg-purple-100', text: 'text-purple-800' },
}

export function StatusBadge({ status, size = 'md' }: StatusBadgeProps) {
  const config = statusConfig[status] || { bg: 'bg-gray-100', text: 'text-gray-800' }

  return (
    <span
      className={clsx(
        'inline-flex items-center font-medium rounded-full',
        config.bg,
        config.text,
        {
          'px-2 py-0.5 text-xs': size === 'sm',
          'px-2.5 py-0.5 text-sm': size === 'md',
        }
      )}
    >
      {status}
    </span>
  )
}
