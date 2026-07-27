const path = require('path');
const CopyWebpackPlugin = require('copy-webpack-plugin');

// Builds only the two CLI entries the CS2 Scripts panel uses. The upstream project
// declares many more (model viewer, avatar, texture tools) that pull in electron/GL
// dependencies this sidecar does not need.
/**
 * @type {import("webpack").Configuration}
 */
module.exports = {
	devtool: false,
	mode: "development",
	entry: {
		cs2export: "./src/headless/cs2export.ts",
		cs2decomp: "./src/headless/cs2decomp.ts"
	},
	module: {
		rules: [
			{
				test: /\.tsx?$/,
				loader: 'ts-loader',
				exclude: /node_modules/,
				options: {
					onlyCompileBundledFiles: true
				}
			},
			{
				test: /\.jsonc?$/,
				type: "asset/source"
			},
			{
				test: /\.glsl(\.c)?$/,
				type: "asset/source"
			}
		],
	},
	target: "node",
	externals: {
		"sqlite3": { commonjs: "sqlite3" },
		"electron": { commonjs: "electron" },
		"electron/main": { commonjs: "electron/main" },
		"electron/renderer": { commonjs: "electron/renderer" },
		"sharp": { commonjs: "sharp" },
		"zlib": { commonjs: "zlib" },
		"lzma": { commonjs: "lzma" },
		"comment-json": { commonjs: "comment-json" },
		"gl": { commonjs: "gl" },
		"canvas": { commonjs: "canvas" },
		"@napi-rs/canvas": { commonjs: "@napi-rs/canvas" }
	},
	resolve: {
		extensions: ['.tsx', '.ts', '.js'],
		modules: [path.resolve(__dirname, 'src'), 'node_modules'],
	},
	externalsType: "commonjs",
	output: {
		libraryTarget: "commonjs",
		filename: "[name].js",
		chunkFilename: "generated/[contenthash].js",
		assetModuleFilename: "generated/[contenthash][ext]",
		webassemblyModuleFilename: "generated/[contenthash][ext]",
		path: path.resolve(__dirname, '..', 'dist')
	},
	plugins: [
		new CopyWebpackPlugin({
			patterns: [
				{ from: 'src/assets', to: "assets" }
			]
		})
	]
};
