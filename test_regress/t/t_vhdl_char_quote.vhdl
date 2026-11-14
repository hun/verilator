-- DESCRIPTION: Verilator: the quote character literal
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

entity t is
  port (
    o : out character
  );
end entity t;

architecture rtl of t is
  -- The character literal for a single quote is three quotes, not an escape
  constant quote : character := ''';
begin
  o <= quote;
end architecture rtl;