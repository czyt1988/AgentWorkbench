// 为 Qt 5.15 内嵌的旧引擎（Chromium 87）补齐现代 agent WebUI 用到的 JS
// 运行时 API。qwen serve / kimi web / dsh web 的前端按现代浏览器目标构建，
// 启动即调用 .at（Chrome 92）、toSorted（110）、Promise.withResolvers
// （119）等新 API，在 Chromium 87 上直接抛 TypeError、页面白屏——这是
// 2026-09 线上日志（~/.AgentWorkbench/log/agentworkbench.log 的 [js]
// CRITICAL 行）的实际症状。
//
// 规则：
// - 只补运行时 API；每一项先做特性检测，新引擎（Qt 6 的 Chromium 118+）
//   上整个文件等于空转，不改变任何行为；
// - 语法级缺口（如 class 静态块，Chrome 94+；正则 /d 标志，90+）无法
//   polyfill——含这类语法的 bundle 解析即失败，由表面的白屏检测落到
//   error 态并引导外置浏览器打开（见 WebEngineSurface.qml）；
// - 本文件自身必须保持在 Chromium 87 可解析的语法内：ES6 可用，
//   ES2021+ 的写法（static 块、#私有字段、逻辑赋值以外的新语法）禁用。
//
// 注入方式：Qt 6 走 profile 级 QWebEngineScript（WebEngineProfileStore），
// Qt 5 的 Quick profile 不继承 core 类、没有 scripts()，走 view 级
// QQuickWebEngineScript（WebEngineCompat::installCompatScript）。两版都是
// MainWorld + DocumentCreation——早于页面任何脚本、页面脚本可见。
(function () {
    "use strict";

    var g = typeof globalThis !== "undefined" ? globalThis : window;

    // 统一以不可枚举方式定义，避免污染 for-in / Object.keys。
    function define(target, name, value) {
        if (target && typeof target[name] === "undefined") {
            Object.defineProperty(target, name, {
                value: value,
                writable: true,
                enumerable: false,
                configurable: true
            });
        }
    }

    // --- Promise.withResolvers（Chrome 119，dsh 的内联脚本直接调用）-------
    if (typeof Promise !== "undefined") {
        define(Promise, "withResolvers", function () {
            var resolve;
            var reject;
            var promise = new Promise(function (res, rej) {
                resolve = res;
                reject = rej;
            });
            return { promise: promise, resolve: resolve, reject: reject };
        });
    }

    // --- .at()（Chrome 92，qwen 的启动路径直接调用）----------------------
    // Array / String / 全部 TypedArray 共用一个实现（语义一致：负数从尾部
    // 数，越界返回 undefined）。
    function atImpl(n) {
        var len = Math.trunc(this.length) || 0;
        if (len < 0) {
            len = 0;
        }
        var i = Math.trunc(n) || 0;
        if (i < 0) {
            i += len;
        }
        if (i < 0 || i >= len) {
            return undefined;
        }
        return this[i];
    }
    var atTargets = [Array.prototype, String.prototype];
    var typedArrayNames = ["Int8Array", "Uint8Array", "Uint8ClampedArray",
                           "Int16Array", "Uint16Array", "Int32Array",
                           "Uint32Array", "Float32Array", "Float64Array",
                           "BigInt64Array", "BigUint64Array"];
    for (var ti = 0; ti < typedArrayNames.length; ++ti) {
        if (typeof g[typedArrayNames[ti]] === "function") {
            atTargets.push(g[typedArrayNames[ti]].prototype);
        }
    }
    for (var tj = 0; tj < atTargets.length; ++tj) {
        if (typeof atTargets[tj].at !== "function") {
            Object.defineProperty(atTargets[tj], "at", {
                value: atImpl,
                writable: true,
                enumerable: false,
                configurable: true
            });
        }
    }

    // --- findLast / findLastIndex（Chrome 97）-----------------------------
    define(Array.prototype, "findLast", function (predicate, thisArg) {
        for (var i = this.length - 1; i >= 0; --i) {
            if (predicate.call(thisArg, this[i], i, this)) {
                return this[i];
            }
        }
        return undefined;
    });
    define(Array.prototype, "findLastIndex", function (predicate, thisArg) {
        for (var i = this.length - 1; i >= 0; --i) {
            if (predicate.call(thisArg, this[i], i, this)) {
                return i;
            }
        }
        return -1;
    });

    // --- 不可变修改族 toSorted / toReversed / toSpliced / with -------------
    // （Chrome 110，kimi 的启动路径直接调用 toSorted。）
    define(Array.prototype, "toSorted", function (compareFn) {
        return Array.prototype.slice.call(this).sort(compareFn);
    });
    define(Array.prototype, "toReversed", function () {
        return Array.prototype.slice.call(this).reverse();
    });
    define(Array.prototype, "toSpliced", function () {
        var out = Array.prototype.slice.call(this);
        Array.prototype.splice.apply(out, arguments);
        return out;
    });
    define(Array.prototype, "with", function (index, value) {
        var out = Array.prototype.slice.call(this);
        var i = Math.trunc(index) || 0;
        if (i < 0) {
            i += out.length;
        }
        if (i < 0 || i >= out.length) {
            throw new RangeError("Invalid index: " + index);
        }
        out[i] = value;
        return out;
    });

    // --- Object.hasOwn（Chrome 93）-----------------------------------------
    define(Object, "hasOwn", function (obj, key) {
        return Object.prototype.hasOwnProperty.call(Object(obj), key);
    });

    // --- Object.groupBy / Map.groupBy（Chrome 117）-------------------------
    define(Object, "groupBy", function (items, keyFn) {
        var out = Object.create(null);
        var list = Array.from(items);
        for (var i = 0; i < list.length; ++i) {
            var key = keyFn(list[i], i);
            if (Object.prototype.hasOwnProperty.call(out, key)) {
                out[key].push(list[i]);
            } else {
                out[key] = [list[i]];
            }
        }
        return out;
    });
    if (typeof Map === "function") {
        define(Map, "groupBy", function (items, keyFn) {
            var out = new Map();
            var list = Array.from(items);
            for (var i = 0; i < list.length; ++i) {
                var key = keyFn(list[i], i);
                var bucket = out.get(key);
                if (bucket) {
                    bucket.push(list[i]);
                } else {
                    out.set(key, [list[i]]);
                }
            }
            return out;
        });
    }

    // --- structuredClone（Chrome 98）---------------------------------------
    // 务实版深克隆：覆盖 UI 代码实际会克隆的类型（原始值、Date、RegExp、
    // ArrayBuffer/视图、Array、Map、Set、普通对象）并正确处理循环引用；
    // Blob/File 降级为引用透传（真克隆需要底层字节复制，UI 场景几乎不会
    // 克隆后再改字节）；函数按规范抛 DataCloneError。
    if (typeof g.structuredClone !== "function") {
        var cloneValue = function (value, seen) {
            if (value === null || typeof value !== "object") {
                return value;
            }
            if (seen.has(value)) {
                return seen.get(value);
            }
            if (value instanceof Date) {
                return new Date(value.getTime());
            }
            if (value instanceof RegExp) {
                return new RegExp(value.source, value.flags);
            }
            if (value instanceof ArrayBuffer) {
                return value.slice(0);
            }
            if (typeof DataView !== "undefined" && value instanceof DataView) {
                return new DataView(value.buffer.slice(0), value.byteOffset,
                                    value.byteLength);
            }
            if (ArrayBuffer.isView(value)) {
                var Ctor = value.constructor;
                return new Ctor(value.buffer.slice(0), value.byteOffset,
                                value.length);
            }
            if (typeof Blob !== "undefined" && value instanceof Blob) {
                return value;
            }
            if (value instanceof Map) {
                var map = new Map();
                seen.set(value, map);
                value.forEach(function (v, k) {
                    map.set(cloneValue(k, seen), cloneValue(v, seen));
                });
                return map;
            }
            if (value instanceof Set) {
                var set = new Set();
                seen.set(value, set);
                value.forEach(function (v) {
                    set.add(cloneValue(v, seen));
                });
                return set;
            }
            if (Array.isArray(value)) {
                var arr = [];
                seen.set(value, arr);
                for (var i = 0; i < value.length; ++i) {
                    arr[i] = cloneValue(value[i], seen);
                }
                return arr;
            }
            // 普通对象：丢弃原型链（与 structuredClone 语义一致）。
            var obj = {};
            seen.set(value, obj);
            var keys = Object.keys(value);
            for (var k = 0; k < keys.length; ++k) {
                obj[keys[k]] = cloneValue(value[keys[k]], seen);
            }
            return obj;
        };
        g.structuredClone = function (value) {
            if (typeof value === "function") {
                throw new DOMException("Cannot clone a function",
                                       "DataCloneError");
            }
            return cloneValue(value, new WeakMap());
        };
    }

    // --- AbortSignal.timeout / AbortSignal.any（Chrome 103 / 116）-----------
    // 87 的 abort() 不接受 reason 参数：传入会被忽略，signal.reason 保持
    // undefined——调用方按惯例把 reason 当可选信息，降级可接受。
    if (typeof g.AbortSignal === "function") {
        define(g.AbortSignal, "timeout", function (ms) {
            var controller = new AbortController();
            setTimeout(function () {
                try {
                    controller.abort(new DOMException("Signal timed out",
                                                      "TimeoutError"));
                } catch (e) {
                    controller.abort();
                }
            }, ms);
            return controller.signal;
        });
        define(g.AbortSignal, "any", function (signals) {
            var controller = new AbortController();
            for (var i = 0; i < signals.length; ++i) {
                (function (signal) {
                    if (signal.aborted) {
                        controller.abort(signal.reason);
                    } else {
                        signal.addEventListener("abort", function () {
                            controller.abort(signal.reason);
                        }, { once: true });
                    }
                })(signals[i]);
            }
            return controller.signal;
        });
    }

    // --- crypto.randomUUID（Chrome 92）--------------------------------------
    if (g.crypto && typeof g.crypto.randomUUID !== "function"
            && typeof g.crypto.getRandomValues === "function") {
        g.crypto.randomUUID = function () {
            var bytes = g.crypto.getRandomValues(new Uint8Array(16));
            bytes[6] = (bytes[6] & 0x0f) | 0x40;   // version 4
            bytes[8] = (bytes[8] & 0x3f) | 0x80;   // variant 10xx
            var hex = [];
            for (var i = 0; i < 16; ++i) {
                hex.push((bytes[i] + 0x100).toString(16).slice(1));
            }
            return hex.slice(0, 4).join("") + "-" + hex.slice(4, 6).join("")
                 + "-" + hex.slice(6, 8).join("") + "-"
                 + hex.slice(8, 10).join("") + "-"
                 + hex.slice(10, 16).join("");
        };
    }

    // --- URL.canParse（Chrome 120）------------------------------------------
    if (typeof g.URL === "function") {
        define(g.URL, "canParse", function (url, base) {
            try {
                new URL(url, base);
                return true;
            } catch (e) {
                return false;
            }
        });
    }

    // --- Response.json 静态构造（Chrome 117）---------------------------------
    if (typeof g.Response === "function" && g.Headers) {
        define(g.Response, "json", function (data, init) {
            var headers = new Headers((init && init.headers) || {});
            if (!headers.has("Content-Type")) {
                headers.set("Content-Type", "application/json");
            }
            var options = {};
            if (init) {
                Object.keys(init).forEach(function (key) {
                    if (key !== "headers") {
                        options[key] = init[key];
                    }
                });
            }
            options.headers = headers;
            return new Response(JSON.stringify(data), options);
        });
    }
})();
