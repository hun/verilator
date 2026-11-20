-- DESCRIPTION: Verilator: VHDL an instance selects the architecture it names
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- The default architecture is the last one analyzed; the instance names `rtl`,
-- so the model must use its value and not the default's. The last instance
-- specializes a generic entity and still selects its architecture.

entity leaf is
  port (y : out integer);
end;

architecture rtl of leaf is
begin
  y <= 1;
end;

architecture alt of leaf is
begin
  y <= 2;
end;

entity gleaf is
  generic (g : integer := 5);
  port (y : out integer);
end;

architecture rtl of gleaf is
begin
  y <= g;
end;

architecture alt of gleaf is
begin
  y <= g + 10;
end;

entity tb is
end;

architecture sim of tb is
  signal y1 : integer := 0;
  signal y2 : integer := 0;
  signal y3 : integer := 0;
begin
  u1 : entity work.leaf(rtl) port map (y => y1);
  u2 : entity work.leaf(alt) port map (y => y2);
  u3 : entity work.gleaf(rtl) generic map (g => 7) port map (y => y3);
  process
  begin
    wait for 1 ns;
    assert y1 = 1 report "selected architecture not used" severity failure;
    assert y2 = 2 report "explicit default architecture not used" severity failure;
    assert y3 = 7 report "specialized architecture not used" severity failure;
    std.env.finish;
    wait;
  end process;
end;
