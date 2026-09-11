/*
    Apollo's frontend build.

    Produces exactly three files — `index.html`, `apollo.js`, `apollo.css` —
    because those are the three the resource provider serves and the three
    `juce_add_binary_data` embeds. Filenames are fixed rather than hashed: there
    is no cache to bust when the page is served out of the plugin binary, and a
    hashed name would mean the C++ resource table had to be regenerated on every
    build.

    ESBUILD RATHER THAN A FRAMEWORK. Apollo needs TypeScript compiled, JSX
    transformed, several dozen modules bundled and a stylesheet emitted; that is
    the whole requirement, and it is what esbuild does in one dependency.
    Choosing Vite or webpack instead would add a dev server Apollo cannot use —
    the page only runs inside the plugin, where it is served from memory — a
    plugin ecosystem nothing here needs, and a few hundred transitive packages to
    audit for a licence review that is a real obligation (CLAUDE.md §32).

    ESBUILD DOES NOT TYPE-CHECK. It strips types and moves on, which is fast and
    is why `npm run check` runs `tsc --noEmit` first. CMake runs both.
*/

import { build, context } from 'esbuild';
import { mkdir, copyFile, rm } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const outDir = process.env.APOLLO_WEBUI_OUT
    ? resolve(process.env.APOLLO_WEBUI_OUT)
    : resolve(here, 'dist');

const watch = process.argv.includes('--watch');

/** @type {import('esbuild').BuildOptions} */
const options = {
    entryPoints: [resolve(here, 'src/main.tsx')],
    bundle: true,
    format: 'iife',
    platform: 'browser',

    // The WebView is Chromium on Windows, WebKit on macOS and Linux, and all
    // three are recent. Targeting es2020 keeps the output readable without
    // down-levelling things every supported runtime has had for years.
    target: ['es2020'],

    outfile: resolve(outDir, 'apollo.js'),
    minify: true,

    // Off deliberately. A source map would more than double what is embedded in
    // the plugin binary, and nothing can consume it: the page is served from
    // memory to a WebView with no developer tools attached in a release build.
    sourcemap: false,

    // React's development build carries warnings, prop-type checks and a much
    // larger bundle. The page ships inside a plugin, so it is always the
    // production build.
    define: { 'process.env.NODE_ENV': '"production"' },

    jsx: 'automatic',
    logLevel: 'info',
    metafile: false,
};

await rm(outDir, { recursive: true, force: true });
await mkdir(outDir, { recursive: true });

if (watch) {
    const ctx = await context(options);
    await ctx.watch();
    await copyFile(resolve(here, 'index.html'), resolve(outDir, 'index.html'));
    console.log('apollo: watching for changes');
} else {
    await build(options);
    await copyFile(resolve(here, 'index.html'), resolve(outDir, 'index.html'));
}
