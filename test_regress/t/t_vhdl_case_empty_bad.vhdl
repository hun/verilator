-- DESCRIPTION: Verilator: VHDL case statements need alternatives
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (
    a : in  integer;
    y : out integer
  );
end;

architecture rtl of t is
begin
  process (a)
  begin
    case a is
    end case;
    y <= 0;
  end process;
end;
