-- DESCRIPTION: Verilator: VHDL character literals only fit character-like types
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A logic character literal is not an integer, real or boolean (IEEE 1076-2008
-- 9.3.2, 15.5).

entity leaf is
  generic (n : integer := '1');
  port (y : out integer);
end;

architecture rtl of leaf is
begin
  y <= n;
end;

entity t is
  port (
    i : in  integer;
    y : out integer
  );
end;

architecture rtl of t is
  function f return integer is
  begin
    return '1';
  end function f;
begin
  -- A relation, a return and a generic default
  y <= 1 when i = '1' else 0;
  y <= f;
  u : entity work.leaf port map (y => open);
end;
