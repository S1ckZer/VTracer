import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import tailwindcss from '@tailwindcss/vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

// We embed the build output directly into the .exe via RCDATA, so we want a
// single self-contained index.html (no <script src="...">, no asset/ folder).
export default defineConfig({
  plugins: [vue(), tailwindcss(), viteSingleFile()],
  build: {
    target: 'es2022',
    cssCodeSplit: false,
    assetsInlineLimit: 100_000_000,
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
      },
    },
  },
  server: {
    port: 5173,
    strictPort: false,
  },
});
