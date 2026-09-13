import { defineConfig } from 'vite';
import path from 'node:path';

export default defineConfig({
  base: '/BTAI/',
  resolve: {
    alias: {
      '@core': path.resolve(__dirname, 'src/core'),
      '@modules': path.resolve(__dirname, 'src/modules'),
      '@scene': path.resolve(__dirname, 'src/scene'),
      '@authoring': path.resolve(__dirname, 'src/authoring'),
    },
  },
});
