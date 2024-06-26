import '../../injectGlobal';
import { MonitorDashboard } from '../../components/monitor';
import { TradingDataDashboard } from '../../components/tradingDataMonitor';

export const initBusEvent = (
  dashboard: MonitorDashboard | TradingDataDashboard,
) => {
  const { message, loader } = dashboard.boards;
  globalThis.globalBus.subscribe((data) => {
    if (message && loader) {
      const { tag, name, status } = data;
      if (tag === 'processStatus') {
        switch (name) {
          case 'system':
            loader.load(`Restart system, Please wait...`);
            break;
          case 'archive':
            loader.stop();
            if (status === 'online') {
              message.log(`Archive success`, 2, (err) => {
                if (err) {
                  console.error(err);
                }
              });
            }
            break;
          case 'master':
            if (status === 'online') {
              message.log(`Master success`, 2, (err) => {
                if (err) {
                  console.error(err);
                }
              });
            }
            break;
          case 'ledger':
            if (status === 'online') {
              message.log(`Ledger success`, 2, (err) => {
                if (err) {
                  console.error(err);
                }
              });
            }
            break;
        }
      }
    }
  });
};
