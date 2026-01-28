import type { Money } from '@/api/transactions'
import { formatCurrency } from '@/hooks/useExchangeRate'
import clsx from 'clsx'

interface MoneyDisplayProps {
  amount: Money | { value: number; currency: string }
  className?: string
  showSign?: boolean
  size?: 'sm' | 'md' | 'lg'
}

export function MoneyDisplay({
  amount,
  className,
  showSign = false,
  size = 'md',
}: MoneyDisplayProps) {
  const value = 'value' in amount ? amount.value : amount.value
  const currency = 'currency' in amount ? amount.currency : amount.currency

  const isPositive = value > 0
  const isNegative = value < 0

  return (
    <span
      className={clsx(
        'font-mono',
        {
          'text-sm': size === 'sm',
          'text-base': size === 'md',
          'text-lg font-semibold': size === 'lg',
          'text-green-600': showSign && isPositive,
          'text-red-600': showSign && isNegative,
        },
        className
      )}
    >
      {showSign && isPositive && '+'}
      {formatCurrency(value, currency)}
    </span>
  )
}

interface BalanceCardProps {
  title: string
  amount: Money | { value: number; currency: string }
  subtitle?: string
  trend?: 'up' | 'down' | 'neutral'
}

export function BalanceCard({ title, amount, subtitle, trend }: BalanceCardProps) {
  return (
    <div className="bg-white rounded-lg border border-gray-200 p-4">
      <p className="text-sm font-medium text-gray-500">{title}</p>
      <div className="mt-1 flex items-baseline gap-2">
        <MoneyDisplay amount={amount} size="lg" />
        {trend && (
          <span
            className={clsx('text-xs font-medium', {
              'text-green-600': trend === 'up',
              'text-red-600': trend === 'down',
              'text-gray-500': trend === 'neutral',
            })}
          >
            {trend === 'up' && '↑'}
            {trend === 'down' && '↓'}
            {trend === 'neutral' && '→'}
          </span>
        )}
      </div>
      {subtitle && <p className="mt-1 text-xs text-gray-400">{subtitle}</p>}
    </div>
  )
}
