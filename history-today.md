# 🕰️ 历史上的 {{MONTH}}月{{DAY}}日

> 副标题：{{HEADLINE}}

> 说明：
> - 这个文档只是结构和脚本示例，没有内置全部 366 天的数据。
> - 要避免捏造历史和版权问题，请自行从可靠来源收集事件，并按下面的 JSON 结构填入。
> - 可以在本地或构建脚本中生成 `history-today-data.json`，供前端脚本读取。

---

下面是前端在网页中使用的示例结构（假设这份 markdown 会被渲染成 HTML，并与同目录下的 `history-today-data.json` 一起部署）：

```jsonc
// history-today-data.json
// 这里只给出少数示例日期；请按同样结构补全所有 366 天。
{
  "01-01": [
    {
      "year": 1949,
      "region": "中国",
      "summary": "中国人民政治协商会议第一届全体会议通过《共同纲领》。"
    },
    {
      "year": 1959,
      "region": "古巴",
      "summary": "古巴革命取得胜利，卡斯特罗领导的武装部队进入哈瓦那。"
    },
    {
      "year": 1801,
      "region": "英国 / 爱尔兰",
      "summary": "《联合法案》生效，大不列颠及爱尔兰联合王国正式成立。"
    },
    {
      "year": 1999,
      "region": "欧盟",
      "summary": "欧元正式启用，成为十一国共同货币的记账单位。"
    },
    {
      "year": 1901,
      "region": "世界",
      "summary": "20 世纪和公认的现代年代学意义上的 20 世纪元年开始。"
    }
  ],
  "03-12": [
    {
      "year": 1912,
      "region": "中国",
      "summary": "孙中山辞去临时大总统职务，袁世凯继任，中华民国进入北洋政府时期。"
    },
    {
      "year": 1930,
      "region": "印度",
      "summary": "圣雄甘地发动食盐进军，掀起反对英国殖民统治的重要非暴力运动。"
    },
    {
      "year": 1994,
      "region": "世界",
      "summary": "首届万维网会议在日内瓦举行，加速了 Web 技术在全球的传播。"
    },
    {
      "year": 1968,
      "region": "毛里塔尼亚 / 毛里求斯",
      "summary": "毛里求斯正式独立，结束英国殖民统治。"
    },
    {
      "year": 2001,
      "region": "土耳其",
      "summary": "土耳其议会通过关键经济改革法案，为之后加入欧盟候选国进程奠定基础。"
    }
  ]
}
```

在 markdown 中，可以嵌入如下脚本，在浏览器端根据今天日期选择并渲染对应记录：

```html
<div id="history-today"></div>
<script>
(function() {
  function pad2(n) { return n < 10 ? "0" + n : "" + n; }
  var today = new Date();
  var key = pad2(today.getMonth() + 1) + "-" + pad2(today.getDate());

  // 根据今天的日期更新标题占位符（如果你的博客引擎支持模板语法，可用其原生方式替换）
  var titleEl = document.querySelector("h1");
  if (titleEl) {
    titleEl.textContent = "🕰️ 历史上的 " + (today.getMonth() + 1) + "月" + today.getDate() + "日";
  }

  fetch("history-today-data.json")
    .then(function(r) { return r.json(); })
    .then(function(data) {
      var list = data[key] || [];
      var container = document.getElementById("history-today");
      if (!container) return;

      if (!list.length) {
        container.textContent = "（当前日期暂无事件，请补充数据。）";
        return;
      }

      // 选出“最有名”的一条做副标题：这里简单取第一条，实际可按年份或权重排序
      var headline = list[0];
      var sub = document.createElement("h2");
      sub.textContent = headline.year + "年 · " + headline.region + " · " + headline.summary;
      container.appendChild(sub);

      var ul = document.createElement("ul");
      list.forEach(function(ev) {
        var li = document.createElement("li");
        li.textContent = ev.year + "年 · " + ev.region + " · " + ev.summary;
        ul.appendChild(li);
      });
      container.appendChild(ul);
    })
    .catch(function(err) {
      console.error("加载 history-today-data.json 失败", err);
    });
})();
</script>
```

> 使用说明：
> - 按上面的 JSON 结构，手动或用脚本从权威来源收集并写入 366 天 × 每天 5 条的事件，保存为 `history-today-data.json`。
> - 将本 markdown 渲染为 HTML 并与该 JSON 放在同一目录，即可在浏览器中按当天日期自动显示对应的“历史上的今天”。
> - 如需在生成型博客（Hexo、Hugo 等）中集成，可把标题、副标题部分改为其模板语法，数据文件和 JS 逻辑保持不变。
