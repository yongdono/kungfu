import { createI18n } from 'vue-i18n';

import zh_CN from './zh-CN';
import en_US from './en-US';

// 语言库
const messages = {
  'zh-CN': zh_CN,
  'en-US': en_US,
};

// 默认语言
const langDefault = 'zh-CN';

const i18n = createI18n({
  locale: langDefault, //默认显示的语言
  messages,
});

export default i18n;
