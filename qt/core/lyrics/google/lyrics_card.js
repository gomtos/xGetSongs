(function () {
  // What the lyrics card of a Google result page shows, read inside the page (Q3, decision 5). The script decides nothing: it
  // reports whether one of the card scopes was there and, for every lyrics line inside it (or in the whole page when there is
  // none), a number for the line's parent element and the line's raw text. parseCard (lyrics_card.cpp) trims, groups and tidies.
  //
  // The answer is JSON.stringify of {"scope": true|false, "lines": [{"p": <parent number>, "t": "<raw text>"}, ...]}:
  //   - scope: whether one of CARD_SCOPES matched (the first one that matches is the scope, as in LyricsCardParser.kt);
  //   - lines: the LINE elements inside the scope in document order; p is equal for lines of one parent element (numbered
  //     from 0 in order of first appearance); t is the text of the line's text nodes in document order, with a line break for
  //     every <br> and without what <script> and <style> elements hold (Jsoup keeps those as data, not text);
  //   - collecting stops once more than MAX_LINES lines or more than MAX_UNITS UTF-16 units of text have been taken (the last
  //     line is cut there), so parseCard, which has the same limits, reads such an answer as a failed extraction.
  // It never throws: anything that goes wrong gives {"failed":true}, which parseCard reads as a failed extraction.
  //
  // CARD_SCOPES, LINE, MAX_LINES and MAX_UNITS must be the values of selectors.h and lyrics_card.h (tst_lyrics_card checks it).
  // Plain ES5, no dependencies: the page is Google's, not ours.

  var CARD_SCOPES = ['div[data-lyricid]', '[jsname=WbKHeb]'];
  var LINE = 'span[jsname=YS01Ge]';
  var MAX_LINES = 5000;
  var MAX_UNITS = 1000000;

  // The node after node in document order inside root, not going into node's children; null at the end of root.
  function nextOutside(node, root) {
    while (node && node !== root) {
      if (node.nextSibling) return node.nextSibling;
      node = node.parentNode;
    }
    return null;
  }

  // The raw text of a line element, at most budget units.
  function textOf(root, budget) {
    var out = '';
    var node = root.firstChild;
    while (node && out.length < budget) {
      if (node.nodeType === 3) {
        out += node.data.substring(0, budget - out.length);
      } else if (node.nodeType === 1) {
        var name = String(node.nodeName).toUpperCase();
        if (name === 'BR') {
          out += '\n';
        } else if (name !== 'SCRIPT' && name !== 'STYLE' && node.firstChild) {
          node = node.firstChild;
          continue;
        }
      }
      node = nextOutside(node, root);
    }
    return out;
  }

  try {
    var scope = null;
    for (var s = 0; s < CARD_SCOPES.length && !scope; s++) scope = document.querySelector(CARD_SCOPES[s]);
    // Jsoup's select also tries the scope element itself; no element can match both a scope and LINE, so querySelectorAll,
    // which does not, finds the same lines.
    var elements = (scope || document).querySelectorAll(LINE);
    var parents = [];
    var lastParent = null;
    var lastNumber = -1;
    var lines = [];
    var units = 0;
    for (var i = 0; i < elements.length && lines.length <= MAX_LINES && units <= MAX_UNITS; i++) {
      var element = elements[i];
      var parent = element.parentNode;
      if (parent !== lastParent) {
        lastNumber = parents.indexOf(parent);
        if (lastNumber < 0) {
          lastNumber = parents.length;
          parents.push(parent);
        }
        lastParent = parent;
      }
      var text = textOf(element, MAX_UNITS - units + 1);
      units += text.length;
      lines.push({ p: lastNumber, t: text });
    }
    return JSON.stringify({ scope: !!scope, lines: lines });
  } catch (e) {
    return '{"failed":true}';
  }
})()
