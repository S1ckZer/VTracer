/// <reference types="vite/client" />

declare module '*.vue' {
  import type { DefineComponent } from 'vue';
  const component: DefineComponent<object, object, unknown>;
  export default component;
}

interface Window {
  chrome?: {
    webview?: {
      postMessage: (msg: string) => void;
      addEventListener: (type: 'message', listener: (event: { data: string }) => void) => void;
      removeEventListener: (type: 'message', listener: (event: { data: string }) => void) => void;
    };
  };
}
